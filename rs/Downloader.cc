/**
 * @biref Pobieracz plikow.
 * @author Piotr Truszkowski
 */

#include <rs/Downloader.hh>
#include <rs/File.hh>
#include <rs/Time.hh>
#include <rs/Http.hh>

#include <pthread.h>

static const size_t PathMaxLen = 1024;

static char Ddir[PathMaxLen]; // pobieranie plikow
static char Sdir[PathMaxLen]; // pobieranie sesji (naglowni http i pliki html)

static bool D_inited = false, S_inited = false;
static FILE *dia;
static FILE *vtmp;
static uint64_t difvtmp_sec = 10000000;

#include <string>
#include <vector>
#include <boost/regex.hpp>
// Tutaj sprawdzamy czy url jest poprawny
static const boost::regex Reg_CorrectUrl("http://rapidshare.com/files/[0-9]*/[a-zA-Z0-9._\\-]*");
// Tutaj szukamy tylko tekstu
static const boost::regex Reg_NotAvailable("This file has been deleted");
static const boost::regex Reg_IllegalFile("This file is suspected to contain illegal content and has been blocked.");
static const boost::regex Reg_NotFound("The file could not be found.");
static const boost::regex Reg_TryLater("Or try again in about ");
static const boost::regex Reg_ServerBusy("Currently a lot of users are downloading files");
static const boost::regex Reg_AlreadyDownloading("is already downloading a file.");
static const boost::regex Reg_ReachedLimit("You have reached the download limit for free-users");
// Tutaj szukamy urla, rozmiaru i czasu oczekiwania
static const boost::regex Reg_Url(
		"<form id=\"ff\" action=\"(http://[a-zA-Z0-9._/\\-]*)\" method=\"post\">");
static const boost::regex Reg_Size(
		"<p class=\"downloadlink\">http://rapidshare.com/files/[0-9]*/[a-zA-Z0-9._\\-]* <font style=\"[a-zA-Z0-9._;:,#\\- ]*\">\\| ([0-9]*) KB</font></p>");
static const boost::regex Reg_Time(
		"var c=([0-9]*);");
// Tutaj szukamy dwoch wartosci, adresu url do pliku i nazwy serwera na ktorym jest plik
static const boost::regex Reg_Server(
		"onclick=\"document.dlf.action=\\\\'(http://[a-zA-Z0-9._/\\-]*)\\\\';\" /> ([a-zA-Z0-9._\\-# ]*)<br />");

static bool Reg_find(const char *str, const boost::regex &reg) throw()
{
	return boost::regex_search(str, reg);
}

static bool Reg_find(const char *str, const boost::regex &reg, std::string &s1) throw()
{
	boost::cmatch what;
	if (!boost::regex_search(str, what, reg)) return false;
	s1 = what[1];
	return true;
}

//static const char *RS_ServerBusy2 = "We regret that currently we have no available slots for free users";
//static const char *RS_ActionUrl2 = "<form name=\"dlf\" action=\"";

static const size_t WaitingForLater    =  60;
static const size_t WaitingForBusy     = 120;
static const size_t WaitingForRivalry  =  60;
static const size_t WaitingForLimit    = 120;

/*** Wyjatki, przy pobieraniu plikow ***/

class DEXC : public std::exception {
	public:
		DEXC(void) throw() { }
		~DEXC(void) throw() { }
		const char *what(void) const throw() { return "DEXC"; }
};

class DAgain : public DEXC {
	public:
		DAgain(void) throw() { }
		~DAgain(void) throw() { }
		const char *what(void) const throw() { return "DAgain"; }
};

class DBreak : public DEXC {
	public:
		DBreak(void) throw() { }
		~DBreak(void) throw() { }
		const char *what(void) const throw() { return "DBreak"; }
};

class DAbort : public DEXC {
	public:
		DAbort(void) throw() { }
		~DAbort(void) throw() { }
		const char *what(void) const throw() { return "DAbort"; }
};

// Ustaw katalog do ktorego zapisywac pliki
void RSDownloader::setDownloadDir(const std::string &path) throw()
{
	int sn = snprintf(Ddir, PathMaxLen, "%s", path.c_str());
	if (sn < 0 || sn >= (int)PathMaxLen) 
		throw EInternal("snprintf");
	
	if (mkdir(Ddir, 0755) && errno != EEXIST) 
		throw EInternal("mkdir: %d, %s", errno, strerror(errno));

	D_inited = true;
}

// Ustaw katalog do ktorego zapisywac strony
void RSDownloader::setSessionsDir(const std::string &path) throw()
{
	int sn = snprintf(Sdir, PathMaxLen, "%s", path.c_str());
	if (sn < 0 || sn >= (int)PathMaxLen)
		throw EInternal("snprintf");

	if (mkdir(Sdir, 0755) && errno != EEXIST) 
		throw EInternal("mkdir: %d, %s", errno, strerror(errno));

	S_inited = true;
}

// Ustaw plik diagnostyczny
void RSDownloader::setDiagnostic(const std::string &path) throw()
{
	dia = fopen(path.c_str(), "a");
}

// Ustaw plik z raportami predkosci chwilowej
void RSDownloader::setSpeedRaporting(const std::string &path, uint32_t difsec) throw()
{
	vtmp = fopen(path.c_str(), "a"); 
	difvtmp_sec = difsec*1000000;
}

// Pobranie instancji
RSDownloader &RSDownloader::instance(void)
{
	static RSDownloader rsd;
	return rsd;
}
	
static pthread_t pth;

RSDownloader::RSDownloader(void) throw() 
{
	m_status  = None;
	m_bytes   = 0;
	m_usecs   = 0;
	m_size    = 0;
	m_speed   = 0;
	m_waiting = 0;

	pthread_attr_t pat;
	int ret;

	if ((ret = pthread_attr_init(&pat)) != 0) 
		throw EInternal("pthread_attr_init: %d, %s", ret, strerror(ret));
	if ((ret = pthread_create(&pth, &pat, RSDownloader::s_thread_fn, NULL)) != 0)
		throw EInternal("pthread_create: %d, %s", ret, strerror(ret));
	if ((ret = pthread_attr_destroy(&pat)) != 0) 
		throw EInternal("pthread_attr_destroy: %d, %s", ret, strerror(ret));

	sleep(1);
}

RSDownloader::~RSDownloader(void) throw()
{
	int ret;

	if ((ret = pthread_cancel(pth)) != 0)
		throw EInternal("pthread_cancel: %d, %s", ret, strerror(ret));
}

void *RSDownloader::s_thread_fn(void *) 
{
	RSDownloader &rsd = RSDownloader::instance();

	rsd.thread_fn();

	return NULL;
}

void RSDownloader::thread_fn(void) throw()
{
	std::string url;

Wait_for:

	m_wait.p();
	
	if (dia) fprintf(dia, "%s - RSD - Zabieramy sie do pobrania pliku '%s'...\n", Time::stamp(), m_url.c_str());

	m_status = Preparing;

Download_it:
	
	for (int tries = 0; tries < 5; ++tries) {

		try { 
			d_stage_1(url); // Ustawimy url na nastepna strone www
			d_stage_2(url); // Ustawimy url na nastepna strone www
			d_stage_3(url); // Sciagamy plik spod url.
		}
		catch (DAgain) { goto Download_it; } // A jeszcze z raz
		catch (DAbort) { goto Aborted; } // Oj, cos powaznego:(
		catch (DBreak) { continue; } // Moze nastepnym razem...

		if (dia) fprintf(dia, "%s - RSD - Pobrano plik '%s', %lluB w %llu.%.3llu sek (%.3f KB/s)\n", 
				Time::stamp(), m_url.c_str(), m_bytes, m_usecs/1000000, (m_usecs/1000)%1000,
				1.0e3*(((double)m_bytes)/((double)m_usecs)));

		m_status = Downloaded; // Ok.
		goto Wait_for;
	}

//Too_many_tries:

	if (dia) fprintf(dia, "%s - RSD - Nie udalo sie pobrac pliku '%s', wyczerpano limit prob\n", Time::stamp(), m_url.c_str());
	m_status = Canceled; // sorry ;P
	
	goto Wait_for;

Aborted:

	if (dia) fprintf(dia, "%s - RSD - Nie udalo sie pobrac pliku '%s', odrzucono zadanie pobierania\n", Time::stamp(), m_url.c_str());
	m_status = Canceled; // sorry ;P
	
	goto Wait_for;
}

void RSDownloader::download(const std::string &url) throw(EAlready, EInvalid)
{
	Lock l(m_lock);

	if (!D_inited || !S_inited) 
		throw EExternal("Nie podano katalogow dokad sciagac dane");

	if (m_status != None && m_status != Downloaded && 
			m_status != Canceled && m_status != NotFound)
		throw EAlready();

	// Gdy brak nazwy pliku lub plik nie pochodzi z http://rapidshare.com
	if (!boost::regex_match(url, Reg_CorrectUrl)) {
		if (dia) fprintf(dia, "%s - RSD - Nieprawidlowy url: %s\n", Time::stamp(), url.c_str());
		throw EInvalid();
	} // Sprawdzamy poprawnosc urla

	m_status = Preparing;
	m_url = url;
	m_bytes = 0;
	m_usecs = 0;
	m_size = 0;
	m_speed = 0.0;
	m_waiting = 0;
	
	m_wait.v();
}

const char *RSDownloader::descr(Status s) throw()
{
	const char *tab[] = {
		/* None        */ "nic do roboty",
		/* Downloaded  */ "plik zostal pobrany",
		/* Canceled    */ "pobieranie pliku zostalo anulowane",
		/* NotFound    */ "nie znaleziono pliku", 
		/* Preparing   */ "przygotowania do pobierania pliku",
		/* Downloading */ "plik jest wlasnie pobierany",
		/* Waiting     */ "oczekiwanie na pobieranie pliku",
		/* Later       */ "sprobuj potem",
		/* Rivalry     */ "ktos inny probuje pobierac pliki",
		/* Limit       */ "wyczerpany limit pobieranych danych",
		/* Busy        */ "serwery sa przeciazone",
		/* Unknown     */ "nieznany blad"
	};

	size_t idx = (size_t)s;
	
	return (idx > Unknown) ? tab[((size_t)Unknown)] : tab[idx];
}

static const char *d_name(const char *url)
{
	return strrchr(url, '/') + 1;
}

static const char *d_download_path(const char *url)
{
	// Tylko jeden watek bedzie korzystal z tej funkcji, zatem
	// jest zmienne statyczne w niej sa bezpieczne.
	
	static char path[PathMaxLen];
	int sn = snprintf(path, PathMaxLen, "%s/%s", 
			Ddir, d_name(url));
	if (sn < 0 || sn >= (int)PathMaxLen) throw EInternal("snprintf");
	
	return path;
}

static const char *d_sessions_path(const char *url, const char *suffix)
{
	// Tylko jeden watek bedzie korzystal z tej funkcji, zatem
	// jest zmienne statyczne w niej sa bezpieczne.
	
	static char path[PathMaxLen];
	int sn = snprintf(path, PathMaxLen, "%s/%s%s", 
			Sdir, d_name(url), suffix);
	if (sn < 0 || sn >= (int)PathMaxLen) throw EInternal("snprintf");
	
	return path;
}

void RSDownloader::wait(Status pre, Status post, size_t secs)
{
	m_lock.lock();
	m_status = pre;
	m_waiting = secs + 1;
	m_lock.unlock();

	while (--m_waiting) sleep(1);

	m_lock.lock();
	m_status = post;
	m_lock.unlock();
}

static void chooseServerFrom(char *buffer, std::string &url)
{
	// Priorytety, ktory serwer najpierw wybrac chcemy:
	static const char *RS_Favorites[] = {
		"TeliaSonera", "Cogent", "GlobalCrossing", "Teleglobe", "Deutsche Telekom", "TeliaSonera #2", 
		"GlobalCrossing #2", "Cogent #2", "Level(3)", "Level(3) #2", "Level(3) #3", "Level(3) #4", NULL
	};

	std::vector<std::pair<std::string, std::string> > srvs;
	for (boost::cregex_iterator it(buffer, buffer+strlen(buffer), Reg_Server), end; it != end; ++it) 
		srvs.push_back(std::make_pair<std::string, std::string>((*it)[2], (*it)[1]));

	if (srvs.size() == 0) {
		if (dia) fprintf(dia, "%s - RSD - Brak serwerow, przerywam... (poziom 2)\n", Time::stamp());
		throw DBreak();
	}

	for (size_t i = 0; RS_Favorites[i]; ++i) { 
		size_t found = 0, end = srvs.size();

		while (found < end) {
			if (srvs[found].first == RS_Favorites[i]) break;
			++found;
		}

		if (found == end) {
			if (dia) fprintf(dia, "%s - RSD - Brak serwera '%s', szukam dalej... (poziom 2)\n", Time::stamp(), RS_Favorites[i]);
			continue;
		}

		if (dia) fprintf(dia, "%s - RSD - Znalazlem i wybralem serwer: '%s' (poziom 2)\n", Time::stamp(), RS_Favorites[i]);
		url = srvs[found].second;

		return;
	}

	url = srvs[0].second;

	if (dia) fprintf(dia, "%s - RSD - Nie znalazlem zadnego z ulubionych serwerow, wybieram pierwszy z proponowanych: '%s'... (poziom 2)\n", Time::stamp(), srvs[0].first.c_str());
}

void RSDownloader::d_stage_1(std::string &url) 
{
	if (dia) fprintf(dia, "%s - RSD - Lacze sie z '%s' (poziom 1)\n", Time::stamp(), m_url.c_str());

	char *buffer = NULL;
	size_t buflen = 0;
	Http http;
	
	http.get(buffer, buflen, m_url.c_str());
	
	try { 
		File body(d_sessions_path(m_url.c_str(), "-body-1.html"));
		body.write(buffer, buflen);
	} catch (...) { }
	try {
		File head(d_sessions_path(m_url.c_str(), "-head-1.html"));
		head.write(http.header(), http.header() ? strlen(http.header()) : 0);
	} catch (...) { }

	if (buffer == NULL || http.error() != Http::Error::None) { // spr bledy
		if (dia) fprintf(dia, "%s - RSD - Blad HTTP: %s (poziom 1)\n", Time::stamp(), http.error());
		if (buffer) delete[] buffer;
		throw DBreak();
	}

	if (http.status() != Http::Status::Ok) { // spr status
		if (dia) fprintf(dia, "%s - RSD - Niepoprawny kod HTTP: %d, (poziom 1)\n", Time::stamp(), http.status());
		delete[] buffer;
		throw DBreak();
	}

	if (Reg_find(buffer, Reg_IllegalFile) ||
			Reg_find(buffer, Reg_NotAvailable) ||
			Reg_find(buffer, Reg_NotFound)) {
		if (dia) fprintf(dia, "%s - RSD - Plik nie jest dostepny\n", Time::stamp());
		delete[] buffer;
		m_status = NotFound;
		throw DAbort();
	}

	if (!Reg_find(buffer, Reg_Url, url)) {
		if (dia) fprintf(dia, "%s - RSD - Nie znaleziono url-a (poziom 1)\n", Time::stamp());
		delete[] buffer;
		throw DBreak();
	}

	delete[] buffer;

	// OK!! Na url mamy link do nastepnej strony!!!
}

void RSDownloader::d_stage_2(std::string &url) 
{
	if (dia) fprintf(dia, "%s - RSD - Lacze sie z '%s' (poziom 2)\n", Time::stamp(), url.c_str());
	
	char *buffer = NULL;
	size_t buflen = 0;
	Http http;
	
	http.get(buffer, buflen, url.c_str(), "dl.start=Free");

	try { 
		File body(d_sessions_path(m_url.c_str(), "-body-2.html"));
		body.write(buffer, buflen);
	} catch (...) { }
	try {
		File head(d_sessions_path(m_url.c_str(), "-head-2.html"));
		head.write(http.header(), http.header() ? strlen(http.header()) : 0);
	} catch (...) { }

	if (buffer == NULL || http.error() != Http::Error::None) { // spr bledy
		if (dia) fprintf(dia, "%s - RSD - Blad HTTP: %s (poziom 2)\n", Time::stamp(), http.error());
		if (buffer) delete[] buffer;
		throw DBreak();
	}

	if (http.status() != Http::Status::Ok) { // spr status
		if (dia) fprintf(dia, "%s - RSD - Niepoprawny kod HTTP: %d (poziom 2)\n", Time::stamp(), http.status());
		delete[] buffer;
		throw DBreak();
	}

	if (Reg_find(buffer, Reg_TryLater)) {
		if (dia) fprintf(dia, "%s - RSD - Trzeba poczekac chwile... (poziom 2)\n", Time::stamp());
		wait(Waiting, Preparing, WaitingForLater);
		delete[] buffer;
		throw DAgain();
	}

	if (Reg_find(buffer, Reg_ReachedLimit)) {
		if (dia) fprintf(dia, "%s - RSD - Wykorzystany limit pobierania plikow (poziom 2)\n", Time::stamp());
		wait(Limit, Preparing, WaitingForLimit);
		delete[] buffer;
		throw DAgain();
	}

	if (Reg_find(buffer, Reg_ServerBusy)) {
		if (dia) fprintf(dia, "%s - RSD - Serwery sa przypchane (poziom 2)\n", Time::stamp());
		wait(Busy, Preparing, WaitingForBusy);
		delete[] buffer;
		throw DAgain();
	}

	if (Reg_find(buffer, Reg_AlreadyDownloading)) {
		if (dia) fprintf(dia, "%s - RSD - Ktos blockuje, ktos teraz pobiera cos... (poziom 2)\n", Time::stamp());
		wait(Rivalry, Preparing, WaitingForRivalry);
		delete[] buffer;
		throw DAgain();
	}
	
	size_t wait_for = 0;
	std::string swait_for;

	if (Reg_find(buffer, Reg_Time, swait_for)) {
		wait_for = strtoul(swait_for.c_str(), 0, 10) + 5;
		if (dia) fprintf(dia, "%s - RSD - Odczekuje %u sek... (poziom 2)\n", Time::stamp(), wait_for);
	} else {
		wait_for = 5;
		if (dia) fprintf(dia, "%s - RSD - Nie wiem ile czekac, zaczekam %u sek... (poziom 2)\n", Time::stamp(), wait_for);
	}

	std::string ssize;

	if (!Reg_find(buffer, Reg_Size, ssize)) { 
		if (dia) fprintf(dia, "%s - RSD - Nie moge znalezc rozmiaru pliku... :( (poziom 2)\n", Time::stamp());
		delete[] buffer;
		throw DBreak();
	}

	m_size = strtoul(ssize.c_str(), 0, 10);

	// Teraz wybierzmy serwer z ktorego chcemy sciagac !
	
	try { chooseServerFrom(buffer, url); } // throw DBreak // 
	catch (DBreak) { delete[] buffer; throw; }
	catch (DEXC &e) { delete[] buffer; throw e; }
	
	if (dia) fprintf(dia, "%s - RSD - Czekam %u sekund przed pobraniem... (poziom 2)\n", Time::stamp(), wait_for);
	wait(Waiting, Preparing, wait_for);

	// Ok!! na url mamy nastepny url
}

static uint64_t progress_bgn = 0;
static uint64_t progress_lst = 0;
static uint64_t progress_bytes = 0;

static void progress_fn_begin(void)
{
	if (vtmp) fprintf(vtmp, "%s 0.000 KB/s\n", Time::stamp());
	
	uint64_t now = Time::in_usec();
	progress_bgn = now;
	progress_lst = now;
	progress_bytes = 0;
}

static void progress_fn_end(void)
{
	uint64_t now = Time::in_usec();
	
	if (progress_bytes > 0 && progress_lst + 1000000 < now) {
		long double sp = ((long double)progress_bytes) / ((long double)(now-progress_lst)) * 1.0e6;
		if (vtmp) fprintf(vtmp, "%s %7u.%.3u KB/s\n", Time::stamp(),((uint32_t)sp)/1000, ((uint32_t)sp)%1000);
	}
	
	if (vtmp) fprintf(vtmp, "%s 0.000 KB/s\n", Time::stamp());
}

bool RSDownloader::progress_fn(const char *, size_t len, void *) 
{
	RSDownloader &rsd = RSDownloader::instance();
	
	uint64_t now = Time::in_usec(), 
					 dif = now - progress_bgn;
	
	progress_bgn = now;
	progress_bytes += len;

	rsd.m_bytes += len;
	rsd.m_usecs += dif;
	rsd.m_speed = ((long double)len) / ((long double)dif) * 1.0e6;

	if (progress_lst + difvtmp_sec < now) {
		long double sp = ((long double)progress_bytes) / ((long double)(now-progress_lst)) * 1.0e6;
		if (vtmp) fprintf(vtmp, "%s %7u.%.3u KB/s\n", Time::stamp(), ((uint32_t)sp)/1000, ((uint32_t)sp)%1000);
		progress_bytes = 0;
		progress_lst = now;
	} 

	return true;
}

void RSDownloader::d_stage_3(std::string &url) 
{
	if (dia) fprintf(dia, "%s - RSD - Laczenie z '%s' (poziom 3)\n", Time::stamp(), url.c_str());
	
	m_status = Downloading;
	progress_fn_begin();
	Http http;

	m_bytes = 0; // Na wszelki wypadek tutaj tez zerujemy dane
	m_usecs = 0; // gdy np wczesniej zerwalo polaczenie podczas
	m_speed = 0; // pobieranie pliku, czy cos tam...

	http.get(d_download_path(m_url.c_str()), url.c_str(), "mirror=", NULL, progress_fn, NULL);

	try {
		File head(d_sessions_path(m_url.c_str(), "-head-3.html"));
		head.write(http.header(), http.header() ? strlen(http.header()) : 0);
	} catch (...) { }

	progress_fn_end();

	if (http.error() != Http::Error::None) { // spr bledy
		if (dia) fprintf(dia, "%s - RSD - Blad HTTP: %s (poziom 3)\n", Time::stamp(), http.error());
		throw DBreak();
	}

	if (http.status() != Http::Status::Ok) {
		if (dia) fprintf(dia, "%s - RSD - Nieprawidlowy kod HTTP: %d (poziom 3)\n", Time::stamp(), http.status());
		throw DBreak();
	}

	m_status = Downloaded;

	// Ok!! Ok!! Ok!!
}



