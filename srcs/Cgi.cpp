#include "Cgi.hpp"

std::string CGI::get_resource_path(const std::string& uri, const std::string& cwd) {
    /* comprueba si la ruta al recurso tiene una raíz absoluta */
    if (!uri.empty() && uri.at(0) == '/') {
	return uri;
    }

    /* monta ruta relativa */
    std::string abs_path(std::string(cwd) + "/" + uri);
    return abs_path;
}

std::string CGI::get_cgi_path(const std::string& cgi_path, const std::string& cwd, const Location& loc)
{
    std::string abs_path;

    /* comprueba si la ruta al CGI ya está configurada como absoluta */
    if (!cgi_path.empty() && cgi_path.at(0) == '/') {
	return cgi_path;
    }

    /* comprueba si root está configurado como absoluto */
    if (loc.get_root().at(0) != '/') {
      abs_path.append(cwd + '/' + loc.get_root());
    } else {
      abs_path.append(loc.get_root());
    }

    if (abs_path.at(abs_path.size() - 1) != '/') {
      abs_path.push_back('/');
    }
    /* añade ruta relativa a CGI */
    abs_path.append(cgi_path);
    return abs_path;
}

void CGI::close_fdIN(void) {
    if (_fdIN[0] != -1) {
        close(_fdIN[0]);
        _fdIN[0] = -1;
    }
    if (_fdIN[1] != -1) {
        close(_fdIN[1]);
        _fdIN[1] = -1;
    }
}

void CGI::close_fdOut(void) {
    if (_fdOut[0] != -1) {
        close(_fdOut[0]);
        _fdOut[0] = -1;
    }
    if (_fdOut[1] != -1) {
        close(_fdOut[1]);
        _fdOut[1] = -1;
    }
}

void CGI::set_env_variables(const std::string& uri/*, const std::string& file_ext*/) {
    std::string tmpBuf;
    int i = 0;

    _envvar[i++] = strdup(("PATH_INFO=" + uri).c_str());
    _envvar[i++] = strdup("SERVER_PROTOCOL=HTTP/1.1");
    // used for php-cgi
    _envvar[i++] = strdup("REDIRECT_STATUS=200");
    if (_req->get_method() == GET){

        // stupid bug in php-cgi
        _envvar[i++] = strdup("REQUEST_METHOD=GET");
//        tmpBuf = "QUERY_STRING=" + _req->get_query();
        if (!_req->get_query().empty()) {
            _envvar[i++] = strdup(std::string("QUERY_STRING=" + _req->get_query()).c_str());
            _envvar[i++] = strdup("CONTENT_TYPE=text/html");
        }
    } else {
        // stupid bug in php-cgi
        _envvar[i++] = strdup("REQUEST_METHOD=POST");	

        std::stringstream intToString;
        intToString << _req->get_request_body().get_body().size();
        tmpBuf = std::string("CONTENT_LENGTH=") + intToString.str();
        _envvar[i++] = strdup(tmpBuf.c_str());

        header_map::const_iterator ct = _req->get_headers().find("Content-Type");
        if (ct != _req->get_headers().end()) {
            _envvar[i++] = strdup(std::string("CONTENT_TYPE=" + ct->second).c_str());
        }
    }
    _envvar[i] = NULL;
}

void CGI::set_args(const std::string& uri, const std::string& cgi_path) {
    _args[0] = (cgi_path.empty()) ? strdup(uri.c_str()) : strdup(cgi_path.c_str());
    _args[1] = (cgi_path.empty()) ? NULL : strdup(uri.c_str());
    _args[2] = NULL;
}

void CGI::set_path_info(const std::string& resource_path) {
    size_t path_separator = resource_path.rfind('/');

    if (path_separator == std::string::npos) {
        _path_info = "/";
        return ;
    }
    _path_info = resource_path.substr(0, path_separator);
}

/* CGIs imprimen como fin de linea tanto LF como CRLF: preparamos la respuesta para normalizarla a un único estándar (LF) */
void CGI::parse_normalize(void) {
  size_t i = 0;

  while ((i = _raw_response.find("\r\n", i)) != std::string::npos) {
    _raw_response.erase(i, 1);
  }
}

/*  Da formato a la respuesta recibida por el CGI y comprueba posibles errores sintácticos:
 * Una respuesta proveniente de un CGI ha de tener al menos un CGI-Header; si
 no está presente el servidoro se repiten lanza respuesta 500 (internal server error)
 * CGI-Header: Content-Type / Location / Status
 * Content-Type DEBE estar presente si existe un cuerpo
 * La presencia de un header Location genera una respuesta 302 (Found)
 * Location puede ser una redirección por parte del servidor o del cliente
 * La presencia de un header Status sobreescribe el estado anterior de la respuesta
 * El código de estado por defecto es 200 OK
 */

void CGI::parse_response_headers(const std::string& headers) {
    static const std::string cgi_header[3] = { "Content-Type", "Location", "Status"};

    std::stringstream headers_ss(headers);
    std::string header_line;

    while (std::getline(headers_ss, header_line)) {
        size_t separator = header_line.find(':');

        if (separator == std::string::npos) {
            continue ;
        }
        std::string header_field(header_line.substr(0, separator++));
        std::string header_value(header_line.substr(header_line.find_first_not_of(' ', separator)));

        if (header_value.empty()) {
            continue ;
        }
	if (!header_field.compare("Content-type")) {
	  header_field = "Content-Type";
        }
        for (int i = 0; i < 3; i++) {
            if (!header_field.compare(cgi_header[i])) {
                if (_header_map.find(header_field) != _header_map.end()) {
                    throw (StatusLine(500, REASON_500, "CGI: parse(), duplicate CGI-header in CGI response"));
                }
                break ;
            }
        }
        _header_map.insert(std::pair<std::string, std::string>(header_field, header_value));
    }
    if (_header_map.find(cgi_header[0]) == _header_map.end() &&
            _header_map.find(cgi_header[1]) == _header_map.end() &&
            _header_map.find(cgi_header[2]) == _header_map.end()) {
        throw (StatusLine(500, REASON_500, "CGI: parse(), missing necessary CGI-Header in response"));
    }
}

void CGI::parse_response_body(const std::string& body) {
    if (!body.empty() && _header_map.find("Content-Type") == _header_map.end()) {
        throw (StatusLine(500, REASON_500, "CGI: parse(), missing body in response with Content-Type header defined"));
    }
    std::map<std::string, std::string>::const_iterator cl = _header_map.find("Content-Length");
    if (cl != _header_map.end()) {
        char *ptr;

        long length = strtol(cl->second.c_str(), &ptr, 0);
        if (length < 0 || *ptr || length != static_cast<long>(body.size())) {
            throw (StatusLine(500, REASON_500, "CGI: parse(), bad content-length definition in CGI response"));
        }
    }
    _body_string = body;
}

/* Status = "Status:" status-code SP reason-phrase NL */

void CGI::parse_status_line(void) {
    std::map<std::string, std::string>::const_iterator lc(_header_map.find("Location"));
    std::map<std::string, std::string>::iterator st(_header_map.find("Status"));

    if (lc != _header_map.end()) {
        _status_line = StatusLine(302, REASON_302, "CGI: redirection");
        return ;
    }
    if (st != _header_map.end()) {
        std::stringstream status_ss(st->second);
        std::string code, reason;
        char *ptr;

        status_ss >> code;
	std::getline(status_ss, reason);
        long status_code = strtol(code.c_str(), &ptr, 0);

        if (!(status_code >= 100 && status_code < 600) || *ptr) {
            throw StatusLine(500, REASON_500, "CGI: parse(), malformed Status-Header");
        }
        _status_line = StatusLine(static_cast<int>(status_code), reason.c_str(), "CGI-defined Status line");
        _header_map.erase(st);
    }
}

/*
 * encuentra separador headers & body
 * substr headers / body
 *  	headers: while (field ':' value NL)
 *	             comprueba sintaxis de la respuesta {al menos un CGIHeader, no repetidos}
 * 		body: si !body.empty() && !content-type -> 500
 *		      si content-length && !(body.size() == content-length) -> 500
 */
void CGI::parse_response(void) {
  parse_normalize();
    size_t separator = _raw_response.find("\n\n");

    if (separator == std::string::npos) {
        throw (StatusLine(500, REASON_500, "CGI: parse(), bad syntax in CGI repsonse"));
    }
    std::string headers = _raw_response.substr(0, separator);
    std::string body = _raw_response.substr(separator + 2);

    parse_response_headers(headers);
    parse_response_body(body);
    parse_status_line();
}

CGI::CGI(Request *req, const std::string& uri, const cgi_pair& cgi_info) :
    _req(req), _status_line(200, REASON_200, "CGI-generated response") {
    /* inicializa atributos de CGI (variables de entorno, argumentos) */
    for (int i = 0; i < 2; i++) {
        _fdIN[i] = -1;
        _fdOut[i] = -1;
    }

    // GET : QUERY_STRING + PATH_INFO 
    // POST : PATH_INFO + CONTENT_length 
    if ((_envvar = new char*[7]) == NULL) {
        throw std::runtime_error("Error on a cgi malloc\n");
    }
    // ** Set args for execve **
    if ((_args = new char*[3]) == NULL) {
        throw std::runtime_error("CGI: " + std::string(strerror(errno)) + "\n");
    }

    /* llamada a getcwd: cambio de malloc a reservar memoria en stack por problemas de fugas de memoria */
    char cwd[PWD_BUFFER];
    if (getcwd(cwd, PWD_BUFFER) == NULL) {
        throw std::runtime_error("CGI: getcwd() call failure");
    }
//    std::string resource_path = std::string(cwd) + "/" + uri;
    std::string resource_path = get_resource_path(uri, cwd);
    std::string cgi_path = get_cgi_path(cgi_info.second, cwd, _req->get_location()); /* ruta absoluta al ejecutable */

    set_env_variables(resource_path/*, cgi_info.first*/);
    set_args(Response::get_filename_from_uri(resource_path), cgi_path);
    set_path_info(resource_path);
}

CGI::~CGI()
{
    int i = -1;
    while (_envvar[++i]){
        free(_envvar[i]); _envvar[i] = NULL;}
    delete[] _envvar;

    i = -1;
    while (_args[++i]){
        free(_args[i]); _args[i] = NULL;}
    delete[] _args;

    close_fdIN();
    close_fdOut();
}

void CGI::executeCGI()
{
    if (pipe(_fdOut) < 0 || pipe(_fdIN) < 0)
        throw StatusLine(500, REASON_500, "pipe failed in executeCGI method");

    pid_t pid = fork();
    if (pid == -1) {
        throw StatusLine(500, REASON_500, "CGI: fork() call error");
    }
    if (!pid){

        // stdout is now a copy of fdOut[1] and in case post method, stdin is a copy of fdIn[0]
        dup2(_fdOut[1], STDOUT_FILENO);
        close_fdOut();

        dup2(_fdIN[0], STDIN_FILENO);
        close_fdIN();

        // change the repo into where the program is
        if (chdir(_path_info.c_str()) == -1) {
            std::cerr << "[CGI error] chdir(): " << strerror(errno) << "\n";
            exit(EXECVE_FAIL);
        }
        if (execve(_args[0], _args, _envvar) < 0){
	    std::cerr << _args[0] << "\n";
	    std::cerr << _args[1] << "\n";
	    std::cerr << _path_info << "\n";
            std::cerr << "[CGI error] execve(): " << strerror(errno) << "\n";
            exit(EXECVE_FAIL);
        }

    }
    close(_fdOut[1]);
    _fdOut[1] = -1;

    if (_req->get_method() == POST) {
        if (write(_fdIN[1], _req->get_request_body().get_body().c_str(), _req->get_request_body().get_body().size()) < 0) {
            throw StatusLine(500, REASON_500, "write failed in executeCGI method");
	}
	std::cerr << "{CALLING WRITE END}\n";
    }
//    size_t 
//    while () {
	
//    }
    close_fdIN();
    char buf[CGI_PIPE_BUFFER_SIZE + 1] = {0};
    int rd_out;
   
    while ((rd_out = read(_fdOut[0], buf, CGI_PIPE_BUFFER_SIZE)) > 0)
    {
	std::cerr << "{CALLING READ}\n";
        _raw_response.append(buf, rd_out);
        memset(buf, 0, CGI_PIPE_BUFFER_SIZE + 1);
    }
    if (rd_out == -1) {
        throw StatusLine(500, REASON_500, std::string("CGI: read() - ") + strerror(errno));
    }
    std::cerr << "{CALLING READ END}\n";
    close_fdOut();

    // Checking if execve correctly worked
    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status) && WEXITSTATUS(status) == EXECVE_FAIL)
    {
        throw StatusLine(500, REASON_500, "execve failed in executeCGI method");
    }
    _raw_response.append(buf, rd_out);
    std::cerr << "{EXITING executeCGI}\n";
}

std::string CGI::getHeaders(void) const {
    std::string header_string;

    for (std::map<std::string, std::string>::const_iterator h = _header_map.begin(); h != _header_map.end(); h++) {
        header_string.append(h->first + ": " + h->second + "\n");
    }
    /* si escribimos el doble salto de línea aquí, llamamos a esta función después de escribir los
     * headers de la respuesta en execCGI */
    header_string.append("\n");
    return header_string;
}

std::string CGI::getBody(void) const {
    return _body_string;
}

const StatusLine& CGI::getStatusLine(void) const {
    return _status_line;
}

bool CGI::isHeaderDefined(const std::string& header_field) const {
    return (_header_map.find(header_field) != _header_map.end());
}

void CGI::mySwap(CGI &a, CGI &b)
{
    std::swap(a._envvar, b._envvar);
    std::swap(a._req, b._req);
    std::swap(a._path_info, b._path_info);
    std::swap(a._raw_response, b._raw_response);
    std::swap(a._status_line, b._status_line);
    std::swap(a._header_map, b._header_map);
    std::swap(a._body_string, b._body_string);
}

CGI::CGI(void) :  _status_line(200, REASON_200, "CGI-generated response") {
    for (int i = 0; i < 2; i++) {
        _fdIN[i] = -1;
        _fdOut[i] = -1;
    }
}

CGI::CGI(const CGI& other) : 
    _req(other._req), _path_info(other._path_info), _raw_response(other._raw_response),
    _status_line(other._status_line), _header_map(other._header_map), _body_string(other._body_string) {
    _envvar = new char*[7];
    _args = new char*[3];
    if (_args == NULL || _envvar == NULL) {
        throw std::runtime_error("CGI: " + std::string(strerror(errno)) + "\n");
    }
    int i = 0;

    for (; other._envvar[i] != NULL; i++) {
        _envvar[i] = strdup(other._envvar[i]);
    }
    _envvar[i] = NULL;
    for (i = 0; other._args[i] != NULL; i++ ) {
        _args[i] = strdup(other._args[i]);
    }
    if (i == 1) _args[i] = NULL;
    _args[2] = NULL;
}
