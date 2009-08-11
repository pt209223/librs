/**
 * @brief Laczenie poprzez protokol http z wykorzystaniem curl-a.
 * @author Piotr Truszkowski
 */

#ifndef __RS_HTTP_HH__
#define __RS_HTTP_HH__

#ifndef _FILE_OFFSET_BITS 
# define _FILE_OFFSET_BITS 64
#elif _FILE_OFFSET_BITS != 64
# error "_FILE_OFFSET_BITS != 64"
#endif

#include <cstdlib>
#include <string>

class Http {
  public:
    // Jesli nie uda sie pobrac strony, do ustawiony bedzie jakis error.
    struct Error {
      typedef const char *Type;
      static const Type None, NoMemory, InvalidArgs, Cancel, Failed, NoWrite, NoAccess, Timeout, NotConnect;
    };

    // None - jeszcze nie laczono,
    // Failed - laczono ale syf,
    // Ok == 200 - laczono i mamy strone
    // i inne kody http 
    struct Status {
      typedef int Type;
      static const Type None = 0, Failed = -1, Ok = 200;
    };

    // Funkcja postepu pobierania - jak zwroci false, pobieranie jest anulowane
    typedef bool (*progress_fn)(const char *buf, size_t len, void *data);
  
    Http(void) throw();
    ~Http(void) throw();
  
    size_t get(char *&page, size_t &len, // Pobierz strone, potem delete[] gdy page != NULL
        const char *url, const char *post = NULL, const char *cookies = NULL, 
        progress_fn fn = NULL, void *data = NULL, int msec = -1) throw();
    off_t get(const char *path, // Pobierz strone do pliku
        const char *url, const char *post = NULL, const char *cookies = NULL,
        progress_fn fn = NULL, void *data = NULL, int msec = -1) throw();
    void clear(void); // Czysc strukture Http

    // Daj naglowek
    const char *header(void) const { return _header; }
    // Daj ciastka
    const char *cookies(void) const { return _cookies; }
    // Dokad jest przekierowanie (==NULL nie ma przekierowania)
    const char *redirect(void) const { return _redirect; }

    Error::Type  error(void) const { return _err; }
    Status::Type status(void) const { return _st; }
    
  private:
    static const size_t _cookies_max_len = 4096;
    static unsigned _timeout_ms;
    char *_header, *_redirect;
    char _cookies[_cookies_max_len];
    Error::Type _err;
    Status::Type _st;
    size_t _hlen, _hreal;

    Http(const Http &);
    static size_t header_fn(void *buf, size_t size, size_t nmemb, void *data);
    static size_t buffer_fn(void *buf, size_t size, size_t nmemb, void *data);
    static size_t file_fn(void *buf, size_t size, size_t nmemb, void *data);
    void set(Status::Type st, Error::Type er) { _st = st; _err = er; }
    void set(Error::Type er) { _err = er; }
    void analyse(void);
};

#endif

