#include "Webserver.hpp"

bool    Webserver::addr_comp::operator()(const Socket& other) {
    return addr == other.get_socket_addr();
}

Webserver::addr_comp::addr_comp(const listen_directive_t& addr) : addr(addr) { }

/* condición de server duplicado: comparten mismas directivas listen y server_name */
void    Webserver::check_server_duplicates(const std::vector<Server>& srv_v) {
    for (std::vector<Server>::const_iterator it = srv_v.begin(); it != --srv_v.end(); it++) {
        for (std::vector<Server>::const_iterator it_n = it + 1; it_n != srv_v.end(); it_n++) {
            if (*it_n == *it) {
                throw std::runtime_error("duplicate server error\n");
            }
        }
    }
}

std::string    Webserver::timestamp(void) const {
    std::string time_fmt;
    std::time_t t = std::time(0);
    std::tm*    time = std::localtime(&t);

    time_fmt += (time->tm_year + 1900) << '-' 
              + (time->tm_mon + 1) << '-'
              +  time->tm_mday;
    return time_fmt;
}

void    Webserver::log(const std::string& error) const {
    std::cerr << "[ " << timestamp() << "]: " << error << "\n";
}

void    Webserver::nfds_up(int fd) {
    if (fd + 1 >= nfds) {
        nfds = fd + 1;
    }
}

void    Webserver::nfds_down(int fd) {
    if (fd + 1 == nfds) {
        --nfds;
    }
}

/* acepta nuevas conexiones de los sockets pasivos; no trata fallos en accept como críticos */
void    Webserver::accept_new_connection(const Socket& passv) {
    struct sockaddr_in addr_in;
    socklen_t addr_len;

    int new_conn = accept(passv.fd, reinterpret_cast<sa_t*>(&addr_in), &addr_len);
    if (new_conn == -1) {
        log (strerror(errno));
        return ;
    }
    fcntl(new_conn, F_SETFL, O_NONBLOCK);
    read_v.push_back(Socket(new_conn, passv));
    nfds_up(read_v.back().fd);
}

request_status_f    Webserver::process_request(Socket& conn, char* buffer) {
    /* comprobar si la petición es nueva*/
    /* si es verdad, inicializar nueva petición */
    /* si no es verdad, continuar rellenando parámetros de petición */
    Request& req = conn.get_request();
    if (req.status() == EMPTY) {
        req.parsingCheck();
    } else {
        /* método que lee chunked body */
        req.parseChunkedRequest();
    }
    /* si, tras la llamada a la construcción de la petición, ya está completada, 
     * construimos la respuesta */
    if (req.status() == IN_PROCESS) {
        return IN_PROCESS;
    }
    conn.set_response(&req, req.getRequestLine, conn.get_server_list());
    req.clean();
    
    return READY;
}

socket_status_f    Webserver::read_from_socket(Socket& conn_socket) {
    char req_buff[REQUEST_BUFFER_SIZE];

    memset(req_buff, 0, REQUEST_BUFFER_SIZE);
    int socket_rd_stat = read(conn_socket.fd, req_buff, REQUEST_BUFFER_SIZE);
    if (socket_rd_stat == -1) {
        /* supón error EAGAIN, la conexión estaba marcada como activa pero ha bloqueado,
         * se guarda a la espera de que el cliente envíe información */
        log(std::strerror(errno));
        return STANDBY;
    }
    if (socket_rd_stat == 0) {
        /* cliente ha cerrado conexión; cerramos socket y eliminamos de la lista */
        conn_socket.close_socket();
        nfds_down(conn_socket.fd);
        //read_v.erase(read_v.begin() + i);
        return CLOSED;
    }
    /* puede ser que una lectura del socket traiga más de una request ?? (std::vector<Request>) */
    /* pipelining; sending multiple requests without waiting for an response */
    request_status_f stat = process_request(conn_socket, req_buff);
    return stat == READY ? CONTINUE : STANDBY;
}

socket_status_f    Webserver::write_to_socket(Socket& conn_socket) {
    /* llamada a write con el mensaje guardado en el Socket */
    /* Response struct with bool indicating if request asked to close connection after sending response */
    /* if request was sent by HTTP 1.0 client and no keep-alive flag was present, close connection */
    Response& resp = conn_socket.get_response();

    int socket_wr_stat = write(conn_socket.fd, resp.getBuffer().c_str(), resp.getSize())
    if (socket_wr_stat == -1) {
        /* supón error EAGAIN, el buffer de write está lleno y como trabajamos con sockets
         * no bloqueadores retorna con señal de error, la respuesta sigue siendo válida y el cliente espera */
        log(strerror(errno));
        return STANDBY;
    }
    if (/* Cliente ha indicado en cabecera que hay que cerrar la conexión */) {
        conn_socket.close_socket();
        return CLOSED;
    }
    resp.clear();
    return CONTINUE;
}

void Webserver::ready_to_read_loop(void) {
    for (size_t i = 0; i < read_v.size(); i++) {
        if (FD_ISSET(read_v[i].fd, &readfds)){
            if (read_v[i].is_passv()) {
                accept_new_connection(read_v[i]);
                continue ;
            }
            socket_status_f conn_stat = read_from_socket(read_v[i]);

            if (conn_stat == CONTINUE) {
                write_v.push_back(read_v[i]);
            }
            if (conn_stat != STANDBY) {
                read_v.erase(read_v.begin() + i);
            }
        }
    }
}

void Webserver::ready_to_write_loop(void) {
    for (size_t i = 0; i < write_v.size(); i++) {
        if (FD_ISSET(write_v[i].fd, &writefds)) {
            socket_status_f conn_stat = write_to_socket(write_v[i]);

            if (conn_stat == CONTINUE) {
                read_v.push_back(write_v[i]);
            }
            if (conn_stat != STANDBY) {
                write_v.erase(write_v.begin() + i);
            }
        }
    }
}

Webserver::Webserver(void) { }

Webserver::Webserver(const Webserver& other) { 
    *this = other;
}

Webserver& Webserver::operator=(const Webserver& other) { 
    if (this == &other) {
        return *this;
    }
    server_v = other.server_v;
    read_v = other.read_v;
    write_v = other.write_v;
    return *this;
}

/* inicializa servidores y sockets pasivos en base a las estructuras generadas por el parser */
Webserver::Webserver(const std::vector<server_block_t>& srv_blk_v) {
    for (std::vector<server_block_t>::const_iterator it = srv_blk_v.begin(); it != srv_blk_v.end(); it++) {
        server_v.push_back(*it);
    }
    check_server_duplicates(server_v);
    for (std::vector<Server>::iterator it = server_v.begin(); it != server_v.end(); it++) {
        Webserver::addr_comp comp(it->get_server_addr());
        std::vector<Socket>::iterator sock_it = std::find_if(read_v.begin(), read_v.end(), comp);

        if (sock_it == read_v.end()) {
            read_v.push_back(it->get_server_addr());
            read_v.back().add_server_ref(*it);
            nfds_up(read_v.back().fd);
        } else {
            sock_it->add_server_ref(*it);
        }
    }
}

Webserver::~Webserver() { }

/* main loop */
void Webserver::run(void) {
    std::cout << "[servidor levantado]\n";
    while (true) {
        FD_ZERO(&readfds);
        FD_ZERO(&writefds);

        for (std::vector<Socket>::iterator it = read_v.begin(); it != read_v.end(); it++) {
            FD_SET(it->fd, &readfds);
        }
        for (std::vector<Socket>::iterator it = write_v.begin(); it != write_v.end(); it++) {
            FD_SET(it->fd, &writefds);
        }
        if ((select(nfds, &readfds, &writefds, NULL, NULL)) == -1) {
            throw std::runtime_error(strerror(errno));
        }
        ready_to_read_loop();
        ready_to_write_loop();
    }
}
