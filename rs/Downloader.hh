/**
 * @brief Pobieracz plikow.
 * @author Piotr Truszkowski
 */

#ifndef __RS_DOWNLOADER_HH__
#define __RS_DOWNLOADER_HH__

#include <string>
#include <rs/Exception.hh>
#include <rs/Mutex.hh>
#include <rs/Semaphore.hh>
#include <stdint.h>

class RSDownloader {
  public:
    /**
     * Klasa singleton - istnieje tylko jeden obiekt tej klasy.
     * Z RapidShare.com mozna sciagac tylko jeden plik naraz.
     */
    static RSDownloader &instance(void);

    /** 
     * @brief Zadanie sciagniecia pliku z zapodanego url-a.
     * 
     * @param url  url do pliku
     *
     * @throw 
     * EAlready - jest juz sciagany jakis plik.
     * EInvalid - niepoprawny url
     */
    void download(const std::string &url) throw(EAlready, EInvalid);

    enum Status {
      None         =  0,  // Nic do roboty
      Downloaded   =  1,  // Plik zostal sciagniety
      Canceled     =  2,  // Sciaganie pliku zostalo anulowane
      NotFound     =  3,  // Nie znaleziono pliku
      Preparing    =  4,  // Przygotowanie do sciagania pliku
      Downloading  =  5,  // Jest sciagany jakis plik
      Waiting      =  6,  // Oczekiwanie na sciagniecie pliku
      Later        =  7,  // Sprobuj potem
      Rivalry      =  8,  // Ktos rowniez probuje sciagac
      Limit        =  9,  // Wyczerpany limit
      Busy         =  10, // Serwery zajete
      Unknown      =  11  // ?
    };

    /** 
     * @brief Funkcja zwracajaca opis statusu.
     */
    static const char *descr(Status s) throw();

    static const size_t UrlMaxLen = 1024;

    /**
     * @brief Pobranie aktualnego statusu
     */
    Status getStatus(void) throw() 
    { 
      Lock l(m_lock); 
      return m_status; 
    }

    /**
     * @brief Pobranie url-a do aktualnie sciaganego pliku.
     */
    void getUrl(std::string &url) throw() 
    { 
      Lock l(m_lock); 
      url = m_url;
    };

    /**
     * @brief Pobranie postepu sciaganego pliku.
     */
    void getProgress(
        Status &status,     // status
        std::string &url,   // url
        uint64_t &bytes,    // Ile bajtow juz pobrano
        uint64_t &usecs,    // Ile czasu trwa pobieranie
        uint64_t &size,     // Rozmiar pobieranego pliku
        long double &speed, // Chwilowa predkosc sciagania
        size_t &waiting     // Czas oczekiwania
        ) throw()
    {
      Lock l(m_lock);
      status = m_status;
      url = m_url;
      bytes = m_bytes;
      usecs = m_usecs;
      size = m_size;
      speed = m_speed;
      waiting = m_waiting;
    }

    /**
     * @brief Ustaw katalog do ktorego zapisywac pliki
     */
    void setDownloadDir(const std::string &path) throw();
    /**
     * @brief Ustaw katalog do ktorego zapisywac strony
     */
    void setSessionsDir(const std::string &path) throw();
    /**
     * @brief Ustaw plik diagnostyczny
     */
    void setDiagnostic(const std::string &path) throw();
    /**
     * @brief Ustaw plik z raportami predkosci chwilowej
     */
    void setSpeedRaporting(const std::string &path, uint32_t difsec = 10) throw();

  private:
    RSDownloader(void) throw();
    RSDownloader(const RSDownloader &);
    ~RSDownloader(void) throw();

    Mutex m_lock;
    Semaphore m_wait;
    Status m_status;
    std::string m_url;
    uint64_t m_bytes, m_usecs, m_size;
    long double m_speed;
    size_t m_waiting;

    void thread_fn(void) throw();
    static void *s_thread_fn(void *);
    static bool progress_fn(const char *buf, size_t len, void *data);

    void d_stage_1(std::string &url);
    void d_stage_2(std::string &url);
    void d_stage_3(std::string &url);
    void wait(Status pre, Status post, size_t secs);
};

#endif

