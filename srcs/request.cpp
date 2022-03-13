#include "request.hpp"

Request::Request() : 
    _buffer(), _index(0), _serv_v(), _headerCount(0), _reqLine(), _headers(), _body(), _stage(REQ_LINE) { }

Request::Request(const std::vector<const Server*>& serv_v) :
    _buffer(), _index(0), _serv_v(serv_v), _headerCount(0), _reqLine(), _headers(), _body(), _stage(REQ_LINE) { }

Request::Request(const Request& c) :
    _buffer(c._buffer), _index(c._index), _serv_v(c._serv_v), _headerCount(c._headerCount),
     _reqLine(c._reqLine), _headers(c._headers), _body(c._body), _stage(c._stage) { }

Request::~Request() { }

Request& Request::operator=(Request a) { 
    if (this == &a) {
        return *this;
    }
    swap(*this, a);
    return *this;
}

const RequestLine& Request::getRequestLine() const {
    return _reqLine;
}
const Request::header_map& Request::getHeaders() const {
    return _headers;
}

const Body& Request::getBody() const {
    return _body;
}

int Request::getMethod() const {
    return _reqLine.getMethod();
}

const std::string& Request::getPath() const {
    return _reqLine.getPath();
}
const std::string& Request::getQuery() const {
    return _reqLine.getQuery();
}

const Location& Request::getLocation(void) const {
    std::vector<const Server*>::const_iterator it = \
        std::find_if(_serv_v.begin(), _serv_v.end(), find_server_by_host(host()));
    const Server* server_host = (it == _serv_v.end()) ? _serv_v.front() : *it;

    return server_host->get_location_by_path(getPath());
}

void Request::setPath(const std::string& path) {
    _reqLine.setPath(path);
}

void swap(Request& a, Request& b) {
   swap(a._buffer, b._buffer);
   std::swap(a._index, b._index);
   std::swap(a._serv_v, b._serv_v);
   std::swap(a._headerCount, b._headerCount);
   swap(a._reqLine, b._reqLine);
   swap(a._headers, b._headers);
   swap(a._body, b._body);
   std::swap(a._stage, b._stage);
}

// Sintaxis:
//        request-line
//        *((header-line) CRLF)
//        CRLF
//        [ message-body ]

void    Request::recvBuffer(const std::string& newBuffer) {
    static req_options req_table[REQ_TAB_SIZE] = {
        &Request::parseRequestLine,
        &Request::parseHeaderLine,
        &Request::parseReqBody,
        &Request::parseChunkReqBody
    };
    bool still_parsing = true;
    std::cout << "[raw req]\n" << newBuffer;

    _buffer.append(newBuffer);
    while (still_parsing == true) {
        still_parsing = (this->*req_table[_stage])(); 
    }
    if (_stage == READY) {
        print();
        throw StatusLine(200, REASON_200, "");
    }
}

bool    Request::parseRequestLine(void) {
    std::string requestLine;

    if (!_getNextLine(requestLine) || requestLine.empty()) {
        THROW_STATUS("syntax error in request-line");
    }
    std::stringstream   lineBuffer(requestLine);
    std::string         token[3];

    for (int i = 0; i < 3; i++) {
        lineBuffer >> token[i];
    }
    if (lineBuffer.rdbuf()->in_avail()) {
        THROW_STATUS("syntax error in request-line");
    }
    parseMethodToken(token[0]);
    parseURI(token[1]);
    parseHTTPVersion(token[2]);
    _stage = HEADER;
    return true;
}

void    Request::parseMethodToken(const std::string& token) {
    std::string method_opts[NB_METHODS] = {"GET", "HEAD", "POST", "DELETE"};

    for (int method_id = 0; method_id < NB_METHODS; method_id++) {
        if (!token.compare(method_opts[method_id])) {
            _reqLine.setMethod(method_id);
            return ;
        }
    }
    THROW_STATUS("unknown method in request");
}

void    Request::parseURI(std::string& token) {
    if (token.size() > MAX_URI_LEN) {
        throw StatusLine(414, REASON_414, "URI exceeded size limit in server configuration");
    }
    std::string allowed_ptcl[2] = { "http://", "https://" };
    size_t queryPos = token.find('?');

    if (queryPos != std::string::npos) {
            _reqLine.setQuery(token.substr(++queryPos));
            token = token.substr(0, --queryPos);
    }
    for (size_t i = 0; i < 2; i++) {
        if (!token.compare(0, allowed_ptcl[i].size(), allowed_ptcl[i])) {
            std::string hostname = token.substr(0, token.find("/", allowed_ptcl[i].size()));
            std::string abs_path = token.substr(hostname.size());

            _headers.insert(std::pair<std::string, std::string>("Host", hostname));
            if (abs_path.empty()) {
                abs_path.append("/");
            }
            _reqLine.setPath(abs_path);
            return ;
        }
    }
    if (token[0] == '/') {
        _reqLine.setPath(token);
        return ;
    }
    THROW_STATUS("bad URI format");
}

void    Request::parseHTTPVersion(const std::string& token) {
    if (token.compare("HTTP/1.1")) {
        THROW_STATUS("unknown or not supported HTTP version");
    }
}

bool    Request::parseHeaderEnd(void) {
    headerMeetsRequirements();
    if (_reqLine.getMethod() == POST) {
        setUpRequestBody();
        _stage = (transferEncodingIsChunked() == true) ? REQ_CHUNK_BODY : REQ_BODY;
    } else {
        _stage = READY;
    }
    return (_stage != READY);
}

bool    Request::parseHeaderLine(void) {
    std::string headerLine;

    if (!_getNextLine(headerLine)) {
        return false;
    }
    if (headerLine.empty()) {
        return parseHeaderEnd();
    }
    if (headerLine.size() > MAX_HEADER_LEN) {
        THROW_STATUS("syntax error on request: header line overflows");
    }
    size_t  colon_pos = headerLine.find(':');

    if (colon_pos == std::string::npos) {   // invalid syntax on header line
        return true;
    }
    std::string fieldName = headerLine.substr(0, headerLine.find(':'));
    std::string fieldValue = headerLine.substr(fieldName.size() + 1);

    if (_headerCount++ > HEADER_LIMIT) {
        THROW_STATUS("too many headers on request");
    }
    for (int i = 0; i < HEADER_LIST_SIZE; i++) {
        if (!fieldName.compare(header_list[i])) {
            _headers.insert(std::pair<std::string, std::string>(fieldName, fieldValue));
            break ;
        }
    }
    return true;
}

void    Request::headerMeetsRequirements(void) const {
    header_map::const_iterator hd = _headers.find("Host");
    header_map::const_iterator cl = _headers.find("Content-Length");
    
    // Debe estar presente un Host definido en la request
    if (hd == _headers.end()) {
        THROW_STATUS("requested host not defined");
    }

    // Si existe un header Content-Length definido, su field-value ha de ser un numero positivo
    if (cl != _headers.end()) {
        char *ptr;

        int contentLengthVal = strtol(cl->second.c_str(), &ptr, 0);
        if (contentLengthVal < 0 || *ptr) {
            THROW_STATUS("invalid value for header Content-Length");
        }
    }
}

void    Request::setUpRequestBody(void) {
    find_server_by_host comp(host());
    header_map::iterator cl = _headers.find("Content-Length");

    if (_serv_v.empty()) {
        throw StatusLine(500, REASON_500, "no virtual server paired with socket");
    }
    _body.setMaxSize(getLocation().get_body_size());
    if (cl != _headers.end()) {
        _body.setSize(std::atoi(cl->second.c_str()));
    }
}

size_t Request::parseChunkSize(void) {
    char*       last;
    std::string sizeToken;

    if (!_getNextLine(sizeToken)) {
        THROW_STATUS("bad chunked-request syntax");
    }
    int size = strtol(sizeToken.c_str(), &last, 0);

    if (sizeToken.empty() || *last || size < 0) {
        THROW_STATUS("bad chunked-request syntax");
    }
    return static_cast<size_t>(size);
}

// Sintaxis SIZE CRLF CHUNK CRLF
// Para el último chunk, sintaxis 0 CRLF CRLF y marcamos Request como preparada para servir
bool    Request::parseChunkReqBody(void) {
    size_t      size = parseChunkSize();
    std::string chunk;

    if (!_getNextLine(chunk) && ((!size && !chunk.empty()) && (size != chunk.size()))) {
        THROW_STATUS("bad chunked-request syntax");
    }
    _body.recvBuffer(chunk);
    if (!size) {
     _stage = READY;
     return false;
    }
    return true;
}

bool    Request::parseReqBody(void) {
    std::string bodyBuffer = _buffer.substr(_index);

    _index += bodyBuffer.size();
    _body.recvBuffer(bodyBuffer);
    if (_body.getBody().size() > contentLength()) {
        THROW_STATUS("request body overflows content-length limit");
    }
    if (_body.getBody().size() == contentLength()) {
     _stage = READY;
     return false;
    }
    return true;
}

bool    Request::transferEncodingIsChunked(void) const {
    header_map::const_iterator te = _headers.find("Transfer-Encoding");
    header_map::const_iterator cl = _headers.find("Content-Length");

    if ((te != _headers.end() && !te->second.compare("chunked")) || (cl == _headers.end())) {
        return true;
    }
    return false;
}

std::string  Request::host(void) const {
    header_map::const_iterator h = _headers.find("Host");

    return (h != _headers.end()) ? h->second : "";
}

// Comportamiento indefinido si se llama antes de pasar por headerMeetsRequirements 
size_t  Request::contentLength(void) const {
    header_map::const_iterator cl = _headers.find("Content-Length");

    if (cl != _headers.end()) {
        return std::atoi(cl->second.c_str());
    }
    return 0;
}

void Request::print(void) const {
    std::cout << "[ request ]\n";
    std::cout << "* -------------------- *\n\n";
    _reqLine.print();
    for (header_map::const_iterator it = _headers.begin(); it != _headers.end(); it++) {
        std::cout << it->first << ": " << it->second << "\n";
    }
    std::cout << "\n" << _body.getBody() << "\n";
    std::cout << "* -------------------- *\n\n";
}

size_t Request::_getNextLine(std::string& line) {
    if (_index == _buffer.size()) {
        return 0; // end of read
    }
    size_t      endLine = _buffer.find(CRLF, _index);

    if (endLine == std::string::npos) {
        throw StatusLine(400, REASON_400, "syntax error found in request: expected CRLF");
    }
    line = _buffer.substr(_index, endLine - _index);
    _index += line.size() + CRLF_OCTET_SIZE;
    return 1;
}

void Request::clear(void) {
    _buffer.clear();
    _index = 0;
    _headerCount = 0;
    _reqLine.clear();
    _headers.clear();
    _body.clear();
    _stage = REQ_LINE;
}

const std::string Request::header_list[HEADER_LIST_SIZE] = {
    "A-IM",             "Accept",          "Accept-Charset",                "Accept-Encoding",
    "Accept-Language",  "Accept-Datetime", "Access-Control-Request-Method", "Access-Control-Request-Headers",
    "Authorization",    "Cache-Control",   "Connection",                    "Content-Length",
    "Content-Type",     "Cookie",          "Date",                          "Expect",
    "Forwarded",        "From",            "Host",                          "If-Match",
    "If-Modified-Since","If-None-Match",   "If-Range",                      "If-Unmodified-Since",
    "Max-Forwards",     "Origin",          "Pragma",                        "Proxy-Authorization",
    "Range",            "Referer",         "TE",                            "Trailer",
    "Transfer-Encoding","User-Agent",      "Upgrade",                       "Via",
    "Warning"
};

