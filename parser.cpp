#include "parser.hpp"

Parser::Parser(void) : raw_input() { }

Parser::Parser(const Parser& other)  {
    *this = other;
}

Parser::~Parser() { }

Parser& Parser::operator=(const Parser& other) {
    if (this == &other) {
        return *this;
    }
    raw_input = other.raw_input;
    for (std::deque<token_t>::const_iterator it = other.tok_lst.begin(); it != other.tok_lst.end(); it++) {
        this->tok_lst.push_back(*it);
    }
    return *this;
}

Parser::token_t::token_t(token_flag_t _ty, const std::string& _tk) : type(_ty), token(_tk) { }

Parser::token_t::token_t(const token_t& other) : type(other.type), token(other.token) { }

location_block_t::location_block_t(void) { }

location_block_t::location_block_t(const std::string& _uri) : uri(_uri) { }

location_block_t::location_block_t(const location_block_t& other) : 
    uri(other.uri) { 
    *this = other;
}

/* falta montar un block default */
location_block_t::location_block_t(const std::vector<std::string> srv_dir[]) {
    uri = "/";

    if (!srv_dir[D_ERROR_PAGE].empty()) {
        dir[D_ERROR_PAGE] = srv_dir[D_ERROR_PAGE];
    }
    if (!srv_dir[]) return ;
}

location_block_t& location_block_t::operator=(const location_block_t& other) {
    if (this == &other) {
        return *this;
    }
    uri = other.uri;
    for (int i = 0; i < N_DIR_LOC; i++) {
        dir[i] = other.dir[i];
    }
    return *this;
}

server_block_t::server_block_t(void) { }

server_block_t::server_block_t(const server_block_t& other) { 
    *this = other;
}

server_block_t& server_block_t::operator=(const server_block_t& other) {
    if (this == &other) {
        return *this;
    }
    loc = other.loc;
    for (int i = 0; i < N_DIR_SRV; i++) {
        dir[i] = other.dir[i];
    }
    return *this;
}

void    server_block_t::setup_default_directives(void) {
    static const std::string default_dir[N_DIR_LOC - 4] = {"0",".","off","index.html","GET"};
    for (int i = 0; i < N_DIR_LOC - 4; i++) {
        if (dir[i].empty()) {
            dir[i].push_back(default_dir[i]);
        }
    }
    for (location_vector::iterator it = loc.begin(); it != loc.end(); it++) {
        location_inherits_from_server(*it);
    }
}

location_block_t&   server_block_t::location_inherits_from_server(location_block_t& loc) {
    for (int i = 0; i < 3; i++) {
        if (loc.dir[i].empty()) {
            loc.dir[i].push_back(dir[i].front());
        }
    }
    if (loc.dir[D_INDEX].empty()) {
        for (std::vector<std::string>::iterator it = dir[D_INDEX].begin(); it != dir[D_INDEX].end(); it++) {
            loc.dir[D_INDEX].push_back(*it);
        }
    }
    if (loc.dir[D_METHOD].empty()) {
        for (std::vector<std::string>::iterator it = dir[D_METHOD].begin(); it != dir[D_METHOD].end(); it++) {
            loc.dir[D_METHOD].push_back(*it);
        }
    }
    if (loc.dir[D_UPLOAD].empty() && !dir[D_UPLOAD].empty()) {
        loc.dir[D_UPLOAD].push_back(dir[D_UPLOAD].front());
    }
    if (loc.dir[D_RETURN].empty() && !dir[D_RETURN].empty()) {
        loc.dir[D_RETURN].push_back(dir[D_RETURN].front());
    }
    if (loc.dir[D_CGI_PASS].empty() && !loc.dir[D_CGI_PASS].empty()) {
        for (std::vector<std::string>::iterator it = dir[D_CGI_PASS].begin(); it != dir[D_CGI_PASS].end(); it++) {
            loc.dir[D_CGI_PASS].push_back(*it);
        }
    }
    if (loc.dir[D_ERROR_PAGE].empty() && !dir[D_ERROR_PAGE].empty()) {
        loc.dir[D_ERROR_PAGE].push_back(dir[D_ERROR_PAGE].front());
    }
}

Parser::token_flag_t Parser::tokenize_id(char c) const {
    static std::string special_char("{};#");
    size_type i = special_char.find(c);

    if (i != std::string::npos) {
        return token_flag_t(i);
    }
    if (std::isspace(c)) {
        return T_SPACE;
    }
    return std::isprint(c) ? T_WORD : T_INVALID_CHAR;
}

Parser::size_type Parser::tokenize_curly_bracket(int pos) {
    std::string  token;
    token_flag_t cb_flag = (raw_input[pos] == '{') ? T_CBO : T_CBC;

    token += raw_input[pos];
    this->tok_lst.push_back(token_t(cb_flag, token));
    return 0;
}

Parser::size_type Parser::tokenize_semicolon(int pos) {
    (void) pos;
    std::string token(";");

    this->tok_lst.push_back(token_t(T_SEMICOLON, token));
    return 0;
}

std::string Parser::tokenize_word_clean_token(const std::string& raw_token) const {
    std::string clean_token(raw_token);
    size_type   pos = 0;
    char        type_q = OFF;

    while ((pos = clean_token.find_first_of("\'\"", pos)) != std::string::npos) {
        if (type_q == clean_token[pos] || type_q == OFF) {
            type_q = (type_q != OFF) ? OFF : clean_token[pos];
            clean_token.erase(pos, 1);
            continue ;
        }
        pos++;
    }
    return clean_token;
}

Parser::size_type Parser::tokenize_word(int pos) {
    const std::string      separator("\x20\x09\x0a\x0b\x0c\x0d;{}#");
    std::string::size_type flag_q = OFF, pos_end = pos;
    char    type_q = OFF;

    while (pos_end < raw_input.size() && 
        (flag_q == ON || separator.find(raw_input[pos_end]) == std::string::npos)) {
            if (flag_q == OFF && ((raw_input[pos_end] == '\'') || raw_input[pos_end] == '\"')) {
                flag_q = ON;
                type_q = raw_input[pos_end];
            } else {
                if (flag_q == ON && raw_input[pos_end] == type_q) {
                    flag_q = OFF;
                }
            }
            pos_end++;
    }
    std::string token = raw_input.substr(pos, pos_end - pos);
    std::string clean_token(tokenize_word_clean_token(raw_input.substr(pos, pos_end - pos)));
    this->tok_lst.push_back(token_t(T_WORD, clean_token));
    return token.size() - 1;
}

size_t Parser::tokenize_space(int pos) {
    size_type   pos_end;

    if (!(raw_input[pos] == '#')){
        pos_end = raw_input.find_first_not_of("\x20\x09\x0a\x0b\x0c\x0d", pos);
    } else {
        pos_end = raw_input.find_first_of("\x0a\x0b\x0c\x0d", pos);
    }
    return pos_end != std::string::npos ? pos_end - (pos + 1): raw_input.size() - pos;
}

void Parser::tokenize(void) {
    const tokenize_options options[N_TOK_TYPE] = {
        &Parser::tokenize_curly_bracket,
        &Parser::tokenize_curly_bracket,
        &Parser::tokenize_semicolon,
        &Parser::tokenize_space,
        &Parser::tokenize_word
    };

    for (size_type i = 0; i < raw_input.size(); i++) {
        token_flag_t token_id = tokenize_id(raw_input[i]);

        if (token_id == T_INVALID_CHAR) {
            throw std::runtime_error("syntax error: found non valid character\n");
        }
        i += (this->*options[token_id])(i);
    }
}

void Parser::read_config_file(const std::string& path) {
    std::ifstream   config_file(path, std::ios::in);

    if (!config_file.is_open()) {
        throw std::runtime_error("could not open configuration file\n");
    }
    std::stringstream buffer;
    buffer << config_file.rdbuf();
    raw_input = buffer.str();
    config_file.close();
}

bool    Parser::is_number(const std::string& str, int n_max, int n_min) const{
    char    *end_ptr;

    long int n = strtol(str.c_str(), &end_ptr, 0);
    return !(*end_ptr || n < n_min || (n_max && n > n_max));
}

bool    Parser::is_addr(const std::string& addr) const {
    if (!addr.compare("localhost")) {
        return true;
    }
    std::vector<std::string> ip_addr;
    std::string ip_octet;
    std::stringstream addr_stream(addr);
    while (std::getline(addr_stream,ip_octet,'.')) {
        ip_addr.push_back(ip_octet);
    }
    for (std::vector<std::string>::iterator it = ip_addr.begin(); it != ip_addr.end(); it++) {
        if (!is_number(*it,256)) {
            return false;
        }
    }
    return ip_addr.size() == 4 ? true : false;
}

bool    Parser::is_word(const token_t& tok) const {
    return (tok.type == T_WORD);
}
bool    Parser::is_semicolon(const token_t& tok) const {
    return (tok.type == T_SEMICOLON);
}
bool    Parser::is_cbo(const token_t& tok) const {
    return (tok.type == T_CBO);
}

bool    Parser::is_cbc(const token_t& tok) const {
    return (tok.type == T_CBC);
}

bool    Parser::syntax_directive_max_body_size(void) const {
    return (is_word(current()) && is_number(current().token) && is_semicolon(peek()));
}

bool    Parser::syntax_directive_rrue(void) const {
    return (is_word(current()) && is_semicolon(peek()));
}

bool    Parser::syntax_directive_autoindex(void) const { 
    return (is_word(current()) && 
           (current().token.compare("on") || current().token.compare("off")) &&
            is_semicolon(peek()));
}

bool    Parser::syntax_directive_index(void) const {
    int count = 0;

    for ( ; is_word(peek(count)); count++) { }
    return (count && is_semicolon(peek(count)));
}

bool    Parser::syntax_directive_cgi_pass(void) const { 
    return (is_word(current()) && is_word(peek()) && is_semicolon(peek(2)));
}

bool    Parser::syntax_directive_accept_method(void) const { 
    int  count = 0;

    for ( ; is_word(peek(count)); count++) {
        if (peek(count).token.compare("GET") &&
            peek(count).token.compare("POST") &&
            peek(count).token.compare("DELETE")) {
            return false;
        }
    }
    return (count && is_semicolon(peek(count)));
}

bool Parser::syntax_directive_server_name(void) const {
    int count = 0;

    for ( ; is_word(peek(count)); count++) { }
    return (count && is_semicolon(peek(count)));
}

bool    Parser::syntax_directive_listen(void) const {
    if (!is_word(current())) {
        return false;
    }
    std::string listen_param(current().token);
    size_type   i(listen_param.find(':'));
    std::string params[2] = {
        (i == std::string::npos) ? listen_param : listen_param.substr(0, i),
        (i == std::string::npos) ? listen_param : listen_param.substr(i + 1, listen_param.size())
    };
    return ((i == std::string::npos ? 
                (is_addr(params[0]) || is_number(params[1], 65535)) :
                (is_addr(params[0]) && is_number(params[1], 65535))) &&
            is_semicolon(peek()));
}

bool    Parser::syntax_directive_location(void) const {
    return (is_word(current()) && is_cbo(peek()));
}

directive_flag_t Parser::syntax_directive(void) {
    int id;

    for (id = 0; id < N_DIR_MAX; id++) {
        if (!current().token.compare(dir_name[id])) {
            break ;
        }
    }
    if (id >= N_DIR_MAX) {
        throw std::runtime_error("dir not recognized in token: \'" + current().token + "\'\n");
    }
    next();
    if (!(this->*dir_options[id])()) {
        throw std::runtime_error("syntax error in dir: \'" + dir_name[id] + "\'\n");
    }
    return directive_flag_t(id);
}

location_block_t   Parser::syntax_location_block(const std::string& uri) {
    if (empty()) throw std::runtime_error("unexpected end of config file\n");

    location_block_t loc(uri);

    for (;!empty() && is_word(current()); next()) {
        directive_flag_t id = syntax_directive();
        if (id >= N_DIR_LOC) {
            throw std::runtime_error("dir not allowed in this context \'" + dir_name[id] + "\'\n");
        }
        for (;!is_semicolon(current()); next()) {
            loc.dir[id].push_back(current().token);
        }
    }
    if (!is_cbc(current())) {
        throw std::runtime_error("unexpected syntax near token \'" + current().token + "\'\n");            
    }
    next();
    return loc;
}

server_block_t&  Parser::syntax_server_block_default(server_block_t& vsrv) {
    static const std::string default_dir[N_DIR_LOC - 4] = {
        "0",
        ".",
        "off",
        "index.html",
        "GET"
    };
    for (int i = 0; i < N_DIR_LOC - 4; i++) {
        if (vsrv.dir[i].empty()) {
            vsrv.dir[i].push_back(default_dir[i]);
        }
    }
    return vsrv;
}

server_block_t    Parser::syntax_server_block(void) {
    server_block_t vsrv;

    while (!empty() && is_word(current())) {
        directive_flag_t id = syntax_directive();
        if (id == D_LOCATION) {
            std::string uri(current().token);
            for (std::vector<location_block_t>::iterator it = vsrv.loc.begin(); it != vsrv.loc.end(); it++) {
                if (!uri.compare(it->uri)) throw std::runtime_error("duplicate dir declared in server \'" + dir_name[id] + "\'\n");
            }
            next();
            next();
            vsrv.loc.push_back(syntax_location_block(uri));
        } else {
            if (!vsrv.dir[id].empty()) {
                throw std::runtime_error("duplicate dir declared in server \'" + dir_name[id] + "\'\n");
            }
            for ( ; !is_semicolon(current()); next()) {
                vsrv.dir[id].push_back(current().token);
            }
            next();
        }
    }
    if (empty() || !is_cbc(current())) {
        throw std::runtime_error("syntax error near unexpected token \'" + current().token + "\'\n");
    }
    next();
    syntax_server_block_default(vsrv);
    return vsrv;
}

server_vector Parser::parse(const std::string& config_path) {
    server_vector vsrv_vector;

    this->read_config_file(config_path);
    this->tokenize();
    if (empty()) {
        return vsrv_vector;
    }

    while (!empty() && is_word(current()) && !current().token.compare("server")) {
        next();
        if (!is_cbo(current())) {
            throw std::runtime_error("syntax error near unexpected token \'" + current().token + "\'\n");
        }
        next();
        vsrv_vector.push_back(syntax_server_block());
    }
    if (!empty()) {
        throw std::runtime_error("syntax error near unexpected token \'" + current().token + "\'\n");
    }
    return vsrv_vector;
}

const std::string Parser::dir_name[N_DIR_MAX] = {
    "error_page",
    "client_max_body_size",
    "root",
    "autoindex",
    "index",
    "return",
    "cgi_pass",
    "accept_method",
    "accept_upload",
    "listen",
    "server_name",
    "location"
};

const Parser::token_t& Parser::current(void) const { 
    return tok_lst.front(); 
}

bool  Parser::empty(void) const { 
    return this->tok_lst.empty();
}

Parser::token_t Parser::peek(size_type pos) const { 
    return (pos <= this->tok_lst.size()) ? this->tok_lst.at(pos) : token_t(T_INVALID_CHAR, "");
}

void Parser::next(void) {
    this->tok_lst.pop_front();
}

Parser::directive_parse_table Parser::dir_options[N_DIR_MAX] = {
    &Parser::syntax_directive_rrue,
    &Parser::syntax_directive_max_body_size,
    &Parser::syntax_directive_rrue,
    &Parser::syntax_directive_autoindex,
    &Parser::syntax_directive_index,
    &Parser::syntax_directive_rrue,
    &Parser::syntax_directive_cgi_pass,
    &Parser::syntax_directive_accept_method,
    &Parser::syntax_directive_rrue,
    &Parser::syntax_directive_listen,
    &Parser::syntax_directive_server_name,
    &Parser::syntax_directive_location
};
