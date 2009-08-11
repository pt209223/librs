/**
 * @brief Bot do pobierania plikow
 * @author Piotr Truszkowski
 */

#include <rs/Downloader.hh>
#include <rs/Time.hh>
#include <rs/File.hh>

#include <fstream>
#include <list>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cassert>

#include <signal.h>

using namespace std;

static const char *Q_primary = "./primary.queue";
static const char *Q_extends = "./extends.queue";
static const char *Q_raports = "./raports.queue";
static const char *Q_tempora = "./tempora.queue";

static size_t load_pri(list<string> &queue)
{
  string url;
  size_t loaded = 0;
  
  fstream qpri(Q_primary, ios::in);  
  
  while (getline(qpri, url)) {
    if (!url.length() || url[0] == '#') continue;
    queue.push_back(url);
    ++loaded;
  }
  
  return loaded;
}

static size_t load_ext(list<string> &queue)
{
  string url;
  size_t loaded = 0;
  
  fstream qext(Q_extends, ios::in);
  
  while (getline(qext, url)) {
    if (!url.length() || url[0] == '#') continue;
    queue.push_back(url);
    ++loaded;
  }

  return loaded;
}

static size_t update_pri(list<string> &queue) 
{
  if (File::exists(Q_tempora)) File::remove(Q_tempora);
  fstream qtmp(Q_tempora, ios::out);

  if (!qtmp.is_open()) {
    fprintf(stderr, 
        "%s - RSB - Nie udalo sie otworzyc pliku '%s', przerywam...\n", 
        Time::stamp(), Q_tempora);
    exit(EXIT_FAILURE);
  }

  for (list<string>::iterator i = queue.begin(); i != queue.end(); ++i) 
    qtmp << i->c_str() << endl;
  qtmp.close();

  if (File::exists(Q_primary)) File::remove(Q_primary);
  if (File::exists(Q_extends)) File::truncate(Q_extends);
  File::rename(Q_tempora, Q_primary);

  return queue.size();
}

int main(int argc, char **argv)
{
  (void)argc;
  (void)argv;

  fprintf(stderr, "%s - RSB - Start !!\n", Time::stamp());
  
  signal(SIGPIPE, SIG_IGN);

  RSDownloader &rsd = RSDownloader::instance();
  
  // Podstawowe ustawienia...

  rsd.setDownloadDir("./d/");
  rsd.setSessionsDir("./s/");
  rsd.setDiagnostic("./rs.dia");
  rsd.setSpeedRaporting("./rs.speed", 10);

  // Kolejka url-i do pobrania...
  list<string> queue;

  if (File::exists(Q_tempora)) {
    // Oho, istnieje plik tymczasowy
    fprintf(stderr, 
        "%s - RSB - Istnieje plik tymczasowy do kolejkowania pobieranych plikow, zrob cos z nim...\n",
        Time::stamp());
    exit(EXIT_FAILURE);
  }

  load_pri(queue);
  load_ext(queue);
  update_pri(queue);

  // Pobieranie plikow z kolejki

  while (true) {
    load_ext(queue);
    update_pri(queue);

    if (queue.empty()) break;
    
    string url = queue.front();
    try { rsd.download(url.c_str()); }
    catch (const EInvalid &) {
      fprintf(stderr, "%s - RSB - Niepoprawny wpis '%s'...\n", Time::stamp(), url.c_str());
      fstream qrap(Q_raports, ios::out|ios::app);
      qrap << "INVALID " << url.c_str() << endl;
      queue.pop_front();
      continue;
    }
    fprintf(stderr, "%s - RSB - Pobieramy plik '%s'...\n", Time::stamp(), url.c_str());

    while (true) {
      RSDownloader::Status status;
      string url2;
      uint64_t bytes, usecs, size;
      long double speed;
      size_t waiting;

      // Patrzymy na postepu sciagania...
      rsd.getProgress(status, url2, bytes, usecs, size, speed, waiting);

      assert(status != RSDownloader::None);
      bool toBreak = false;

      switch (status) {
        case RSDownloader::Downloaded: 
          { // Sciagnieto plik
            toBreak = true;
            fprintf(stderr, "\n"
                "%s - RSB - Plik zostal pobrany, %6llu.%.3llu KB w %llu:%.2llu:%.2llu sek (%4llu.%.3llu KB/s)\n",
                Time::stamp(), bytes/1000, bytes%1000, usecs/3600000000ULL, (usecs/60000000)%60, (usecs/1000000)%60,
                (1000 * bytes / usecs), (1000000 * bytes / usecs)%1000);

            fstream qrap(Q_raports, ios::out|ios::app);
            qrap << "OK " << url.c_str() << endl;
          }
          break;
        case RSDownloader::Canceled:
          { // Anulowano sciaganie pliku
            toBreak = true;
            fprintf(stderr, "\n"
                "%s - RSB - Anulowano pobieranie tego pliku...\n",
                Time::stamp());

            fstream qrap(Q_raports, ios::out|ios::app);
            qrap << "CANCEL " << url.c_str() << endl;
          }
          break;
        case RSDownloader::NotFound:
          { // Nie znaleziono pliku
            toBreak = true;
            fprintf(stderr, "\n"
                "%s - RSB - Nie znaleziono takiego pliku w serwisie...\n",
                Time::stamp());

            fstream qrap(Q_raports, ios::out|ios::app);
            qrap << "NOTFOUND " << url.c_str() << endl;
          }
          break;
        case RSDownloader::Downloading:
          { // Plik jest pobierany
            uint64_t eta = (bytes) ? ((size*1000ULL - bytes) * usecs / bytes / 1000000ULL ) : 0ULL,
                     v1 = usecs ? (1000ULL*bytes/usecs) : 0ULL, v2 = usecs ? ((1000000ULL*bytes/usecs)%1000ULL) : 0ULL,
                     th = usecs/3600000000ULL, tm = (usecs/60000000ULL)%60, ts = (usecs/1000000ULL)%60,
                     eh = eta/3600ULL, em = (eta/60ULL)%60ULL, es = eta%60ULL;
            fprintf(stderr, 
                "%s - RSB - Postep: %6llu.%.3llu KB %llu:%.2llu:%.2llu (srd: %4llu.%.3llu KB/s chw: %4llu.%.3llu KB/s roz: %6llu KB ETA: %llu:%.2llu:%.2llu)   \r",
                Time::stamp(), bytes/1000ULL, bytes%1000ULL, th, tm, ts, v1, v2, ((uint64_t)speed)/1000ULL, ((uint64_t)speed)%1000ULL, size, eh, em, es);
          }
          break;
        case RSDownloader::Preparing:
          { // Trwaja przygotowania
            fprintf(stderr,
                "%s - RSB - Przygotowania do pobierania pliku...                                                                               \r",
                Time::stamp());
          }
          break;
        case RSDownloader::Waiting:
        case RSDownloader::Later:
        case RSDownloader::Rivalry:
        case RSDownloader::Limit:
        case RSDownloader::Busy:
          {
            fprintf(stderr, 
                "%s - RSB - Status: '%s' , oczekuje: %u sek                                                                                    \r",
                Time::stamp(), RSDownloader::descr(status), waiting);
          }
          break;
        default:
          throw EInternal("Nieprawidlowy status: %d, %s", 
              status, RSDownloader::descr(status));
      };

      if (toBreak) { queue.pop_front(); break; }

      usleep(250000);
    }
  }

  // Kolejka jest pusta

  fprintf(stderr, 
      "%s - RSB - Brak plikow do pobierania. Koniec !!\n",
      Time::stamp());

  return EXIT_SUCCESS;
}

