/**
 * @brief Laczenie poprzez protokol http z wykorzystaniem curl-a.
 * @author Piotr Truszkowski
 */

#include <curl/curl.h>

#include <rs/Http.hh>
#include <rs/Exception.hh>

#include <iostream>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
using std::cerr;
using std::endl;

const Http::Error::Type Http::Error::None = "No errors";
const Http::Error::Type Http::Error::NoMemory = "Not enought memory";
const Http::Error::Type Http::Error::InvalidArgs = "Invalid arguments";
const Http::Error::Type Http::Error::Cancel = "Operation canceled";
const Http::Error::Type Http::Error::Failed = "Operation failed";
const Http::Error::Type Http::Error::NoWrite = "Cannot write buffer to file";
const Http::Error::Type Http::Error::NoAccess = "Cannot open file";
const Http::Error::Type Http::Error::Timeout = "Timeout on connect";
const Http::Error::Type Http::Error::NotConnect = "Couldn't connect";

unsigned Http::_timeout_ms = 10000;

Http::Http(void) throw()
	: _header(NULL), _redirect(NULL), 
	_err(Error::None), _st(Status::None), 
	_hlen(0), _hreal(0) { _cookies[0] = 0; }

Http::~Http(void) throw() { clear(); }

struct buffer_task {
	Http *http;
	char **page;
	size_t &len;
	size_t real;
	Http::progress_fn fn;
	void *data;
	
	buffer_task(Http *http, char **page, size_t &len, Http::progress_fn fn, void *data)
		: http(http), page(page), len(len), real(0), fn(fn), data(data) { }
	~buffer_task(void) { }
};

struct stream_task {
	Http *http;
	int fd;
	Http::progress_fn fn;
	void *data;
	off_t len;

	stream_task(Http *http, int fd, Http::progress_fn fn, void *data)
		: http(http), fd(fd), fn(fn), data(data), len(0) { }
	~stream_task(void) throw() 
	{
		if (fd >= 0 && close(fd)) throw EInternal(strerror(errno));
	}
};

size_t def_real_header = 4096,
			 def_real_data = 16384;

size_t Http::header_fn(void *buf, size_t size, size_t nmemb, void *data) {
	size_t sz = size*nmemb;
	Http *h = (Http*)data;
	
	if (h->_hreal == 0) {
		size_t max = def_real_header < sz+1 ? sz+1 : def_real_header;

		char *ptr = (char *)malloc(max);
		if (!ptr) { h->set(Error::NoMemory); return CURLE_WRITE_ERROR; }

		h->_header = ptr;
		h->_hreal = max;
	} else if (h->_hlen + sz + 1 > h->_hreal) {
		size_t df = h->_hlen + sz + 1 > h->_hreal;
		size_t max = h->_hreal + (h->_hreal < df ? df : h->_hreal);
	
		char *ptr = (char*)realloc(h->_header, max);
		if (!ptr) { h->set(Error::NoMemory); return CURLE_WRITE_ERROR; }
		
		h->_header = ptr;
		h->_hreal = max;
	}
	
	memcpy(h->_header + h->_hlen, buf, sz);
	h->_hlen += sz;
	h->_header[h->_hlen] = 0;

	return sz;
}

size_t Http::buffer_fn(void *buf, size_t size, size_t nmemb, void *data) {
	size_t sz = size*nmemb;
	buffer_task *tsk = (buffer_task*)data;
	
	if (tsk->real == 0) {
		size_t max = def_real_data < sz+1 ? sz+1 : def_real_data;
		
		if (!((*tsk->page) = new(std::nothrow) char[max])) {
			tsk->http->set(Error::NoMemory); 
			return CURLE_WRITE_ERROR; 
		}
		
		tsk->real = max;
	} else if (tsk->len + sz + 1 > tsk->real) {
		size_t df = tsk->len + sz + 1 > tsk->real;
		size_t max = tsk->real + (tsk->real < df ? df : tsk->real);
	
		char *ptr = new(std::nothrow) char[max];
		if (!ptr) { tsk->http->set(Error::NoMemory); return CURLE_WRITE_ERROR; }
		memcpy(ptr, *tsk->page, tsk->len);
		delete[] (*tsk->page);
		
		(*tsk->page) = ptr;
		tsk->real = max;
	}
	
	memcpy((*tsk->page) + tsk->len, buf, sz);
	tsk->len += sz;
	(*tsk->page)[tsk->len] = 0;
	
	if (tsk->fn && !tsk->fn((const char*)buf, sz, tsk->data)) {
		tsk->http->set(Error::Cancel);
		return CURLE_WRITE_ERROR;
	}

	return sz;
}

size_t Http::file_fn(void *buf, size_t size, size_t nmemb, void *data) {
	size_t sz = size*nmemb;
	stream_task *tsk = (stream_task*)data;
	Http *h = tsk->http;
	int fd = tsk->fd;

	size_t done = 0;

	while (done < sz) {
		int wr = write(fd, (char*)buf+done, sz-done);
		if (wr < 0) {
			if (errno == EINTR || errno == EAGAIN) continue;
			h->set(Error::NoWrite); 
			return CURLE_WRITE_ERROR;
		}
		done += wr;
	}

	if (tsk->fn && !tsk->fn((const char*)buf, sz, tsk->data)) {
		h->set(Error::Cancel);
		return CURLE_WRITE_ERROR;
	}
	
	tsk->len += sz;
	return sz;
}

static curl_slist *MoreHeaders(void) throw()
{
	static curl_slist *slist = NULL;
	if (!slist) {
		if (!(slist = curl_slist_append(slist, "User-Agent: Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.8) Gecko/20071028 PLD/3.0 (Th) BonEcho/2.0.0.8")))
			throw EInternal("curl_slist_append() - fix it!");
		if (!(slist = curl_slist_append(slist, "Keep-Alive: 300")))
			throw EInternal("curl_slist_append() - fix it!");
		if (!(slist = curl_slist_append(slist, "Connection: close"))) 
			throw EInternal("curl_slist_append() - fix it!");
	}
	// FIXME : potrzeba jeszcze curl_slist_free_all(slist)
	return slist;
}

size_t Http::get(char *&page, size_t &len, 
		const char *url, const char *post, const char *cookies, 
		Http::progress_fn fn, void *data, int msec) throw() 
{
	clear();
	page = NULL;
	len = 0;

	_st = Status::Failed;
	_err = Error::Failed;

	CURL *curl = curl_easy_init();
	if (!curl) { _err = Error::NoMemory; return -1; }

	buffer_task tsk(this, &page, len, fn, data);

	if (curl_easy_setopt(curl, CURLOPT_URL, url) != CURLE_OK ||
			(post && curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post) != CURLE_OK) ||
			(cookies && curl_easy_setopt(curl, CURLOPT_COOKIE, cookies) != CURLE_OK) ||
			(msec > 0 && curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, msec) != CURLE_OK) ||
			curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, _timeout_ms) != CURLE_OK ||
			curl_easy_setopt(curl, CURLOPT_HTTPHEADER, MoreHeaders()) != CURLE_OK ||
//			curl_easy_setopt(curl, CURLOPT_VERBOSE, 1) != CURLE_OK ||
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, buffer_fn) != CURLE_OK ||
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &tsk) != CURLE_OK ||
			curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_fn) != CURLE_OK ||
			curl_easy_setopt(curl, CURLOPT_HEADERDATA, this) != CURLE_OK) {
		curl_easy_cleanup(curl);
		_err = Error::InvalidArgs;
		return -1;
	}
	
	CURLcode cd = curl_easy_perform(curl);
	if (cd != CURLE_OK) {
		if (cd == CURLE_OPERATION_TIMEOUTED) _err = Error::Timeout;
		else if (cd == CURLE_COULDNT_CONNECT) _err = Error::NotConnect;
		else _err = Error::Failed;

		curl_easy_cleanup(curl);
		return -1;
	}

	curl_easy_cleanup(curl);
	
	analyse();

	return _err == Error::None ? tsk.len : -1;
}

off_t Http::get(const char *path,
		const char *url, const char *post, const char *cookies, 
		Http::progress_fn fn, void *data, int msec) throw() 
{
	clear();

	_st = Status::Failed;
	_err = Error::Failed;

	int fd = open(path, O_WRONLY|O_TRUNC|O_CREAT, 0644);
	if (fd < 0) { _err = Error::NoAccess; return -1; }

	// destruktor stream_task zamknie fd
	stream_task tsk(this, fd, fn, data);
	
	CURL *curl = curl_easy_init();
	if (!curl) { _err = Error::NoMemory; return -1; }

	if (curl_easy_setopt(curl, CURLOPT_URL, url) != CURLE_OK ||
			(post && curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post) != CURLE_OK) ||
			(cookies && curl_easy_setopt(curl, CURLOPT_COOKIE, cookies) != CURLE_OK) ||
			(msec > 0 && curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, msec) != CURLE_OK) ||
			curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, _timeout_ms) != CURLE_OK ||
			curl_easy_setopt(curl, CURLOPT_HTTPHEADER, MoreHeaders()) != CURLE_OK ||
//			curl_easy_setopt(curl, CURLOPT_VERBOSE, 1) != CURLE_OK ||
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, file_fn) != CURLE_OK ||
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &tsk) != CURLE_OK ||
			curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_fn) != CURLE_OK ||
			curl_easy_setopt(curl, CURLOPT_HEADERDATA, this) != CURLE_OK) {
		curl_easy_cleanup(curl);
		_err = Error::InvalidArgs;
		return -1;
	}
	
	CURLcode cd = curl_easy_perform(curl);
	if (cd != CURLE_OK) {
		if (cd == CURLE_OPERATION_TIMEOUTED) _err = Error::Timeout;
		else if (cd == CURLE_COULDNT_CONNECT) _err = Error::NotConnect;
		else _err = Error::Failed;
	
		curl_easy_cleanup(curl);
		return -1;
	}

	curl_easy_cleanup(curl);
	analyse();

	return _err == Error::None ? tsk.len : -1;
}


void Http::clear(void) {
	if (_header) { free(_header); _header = NULL; }
	if (_redirect) { free(_redirect); _redirect = NULL; }
	_cookies[0]  = 0;
	_hlen = _hreal = 0;
	_st = 0;
}

void Http::analyse(void) {
	if (_hlen < 10) return set(Status::Failed, Error::Failed);
	
	_st = Status::Failed;
	_err = Error::None;

	char *ptr = _header, *eol;
	size_t c_len = 0, c_max = _cookies_max_len;
	char *c_buf = _cookies;

	while ((eol = strstr(ptr, "\r\n"))) {
		if (strncasecmp(ptr, "HTTP/", 5) == 0) {
			ptr += 5;
			while (ptr != eol && *ptr != ' ' && *ptr != '\t') ++ptr;
			if (ptr == eol) set(Status::Failed, Error::Failed);
			while (ptr != eol && (*ptr == ' ' || *ptr == '\t')) ++ptr;
			_st = strtoul(ptr, NULL, 10);
			
			if (_st < 100 || _st > 700) set(Status::Failed, Error::Failed);
		} else if (strncasecmp(ptr, "Location:", 9) == 0) {
			ptr += 9;
			while (ptr != eol && (*ptr == ' ' || *ptr == '\t')) ++ptr;
			*eol = 0;
			_redirect = strdup(ptr);
			*eol = '\r';
			if (!_redirect) { _err = Error::NoMemory; return; }
		} else if (strncasecmp(ptr, "Set-Cookie:", 11) == 0) {
			ptr += 11;
			while (ptr != eol && (*ptr == ' ' || *ptr == '\t')) ++ptr;
			char *eoc = ptr;
			while (eol != eoc && *eoc != ';') ++eoc;
			if (eol == eoc) set(Status::Failed, Error::Failed);
			char c = *eoc;
			*eoc = 0;
			size_t l = strlen(ptr);
			*eoc = c;
		
			if (c_len + l + 3 > c_max) { _err = Error::NoMemory; return; }
			strncpy(c_buf+c_len, ptr, l);
			strncpy(c_buf+c_len+l, "; ", 2);
			
			c_len += l + 2;
			c_buf[c_len] = 0;
		}

		ptr = eol + 2;
	}
}

