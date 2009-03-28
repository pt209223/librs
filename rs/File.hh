/**
 * @brief Implementacja podstawowych operacji na plikach
 * @author Piotr Truszkowski
 */

#ifndef __RS_FILE_HH__
#define __RS_FILE_HH__

/**
 * Obsluga duzych plikow sizeof(off_t) == 8
 */
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#elif _FILE_OFFSET_BITS != 64
#warn Powinna byc wlaczona obsluga duzych plikow 
#endif

#include <rs/Exception.hh>
#include <rs/Time.hh>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

class File {
	public:
		// Tryb pracy na pliku
		typedef int Mode;
		static const Mode Read     = 0x01;
		static const Mode Write    = 0x02;
		static const Mode Trunc    = 0x04;
		static const Mode Creat    = 0x08;
		static const Mode Append   = 0x10;

		// Sposob przesuniecia pozycji w pliku
		typedef int Whence;
		static const Whence Set = SEEK_SET;
		static const Whence End = SEEK_END;
		static const Whence Cur = SEEK_CUR;

		// Wyjatki
		DEF_EXC( EFile        , Exception );
		DEF_EXC( ENoOpen      , EFile     );
		DEF_EXC( ENoAccess    , EFile     );
		DEF_EXC( ENoSpace     , EFile     );
		DEF_EXC( ENotForRead  , EFile     );
		DEF_EXC( ENotForWrite , EFile     );
		DEF_EXC( ENotExists   , EFile     );
		DEF_EXC( EExists      , EFile     );

		File(void) 
			throw() : m_fd(-1), m_mode(0) { }
		File(const char *path, Mode m = Read|Write|Creat) 
			throw(EFile) : m_fd(-1), m_mode(0) { open(path, m); }
		virtual ~File(void) throw() { close(); }

		virtual bool is_open(void) const { return m_fd != -1; }
		virtual bool is_closed(void) const { return m_fd == -1; }
		virtual bool is_readable(void) const { return ( m_mode & Read ); }
		virtual bool is_writeable(void) const { return ( m_mode & Write ); }

		virtual void open(const char *path, Mode mode = Read|Write) throw(EFile)
		{
			close();
			if (!(mode&Read) && !(mode&Write)) mode |= Read;

			int m = 0; // Musimy przetlumaczyc na fnctl
			m |= ((mode&Read) && (mode&Write)) ? O_RDWR : ((mode&Write) ? O_WRONLY : O_RDONLY);
			m |= (mode&Creat) ? O_CREAT : 0;
			m |= (mode&Trunc) ? O_TRUNC : 0;
			m |= (mode&Append) ? O_APPEND : 0;

			m_fd = (mode & Creat) ? ::open(path, m, 0644) : ::open(path, m);

			if (m_fd == -1) {
				if (errno == EACCES || errno == EPERM) throw ENoAccess();
				if (errno == ENOENT || errno == ENOTDIR) throw ENotExists();
				throw EInternal("FileBinary::open: %d, %s", errno, strerror(errno));
			}

			m_mode = mode;
		}

		virtual void close(void) throw()
		{
			if (m_fd != -1) {
				if (::close(m_fd)) 
					throw EInternal("FileBinary::close: %d, %s", errno, strerror(errno));
				m_fd = -1;
				m_mode = 0;
			}
		}

		virtual void read(void *buf, size_t len) throw(EFile)
		{
			if (m_fd == -1) throw ENoOpen();
			if (!(m_mode & Read)) throw ENotForRead();
			
			size_t done = 0;

			while (done < len) {
				int rd = ::read(m_fd, ((char*)buf)+done, len-done);
				if (rd < 0) {
					if (errno == EINTR || errno == EAGAIN) continue;
					throw EInternal("FileBinary::read: %d, %s", errno, strerror(errno));
				}
				done += rd;
			}
		}

		virtual void write(const void *buf, size_t len) throw(EFile)
		{
			if (m_fd == -1) throw ENoOpen();
			if (!(m_mode & Write)) throw ENotForWrite();
			
			size_t done = 0;

			while (done < len) {
				int wr = ::write(m_fd, ((const char*)buf)+done, len-done);
				if (wr < 0) {
					if (errno == EINTR || errno == EAGAIN) continue;
					if (errno == ENOSPC) throw ENoSpace();
					throw EInternal("FileBinary::write: %d, %s", errno, strerror(errno));
				}
				done += wr;
			}
		}

		virtual void sync(void) throw()
		{
			if (m_fd != -1 && fsync(m_fd)) 
				throw EInternal("FileBinary::sync: %d, %s", errno, strerror(errno));
		}

		virtual off_t seek(off_t offset = 0, Whence w = Set) throw(EFile)
		{
			if (m_fd == -1) throw ENoOpen();

			off_t noff = lseek(m_fd, offset, w);
			if (noff != -1) 
				throw EInternal("FileBinary::seek: %d, %s", errno, strerror(errno));

			return noff;
		}

		virtual off_t size(void) throw(EFile)
		{
			if (m_fd == -1) throw ENoOpen();

			struct stat st;
			if (fstat(m_fd, &st)) 
				throw EInternal("FileBinary::size: %d, %s", errno, strerror(errno));

			return st.st_size;
		}

		static void copy(const char *src, const char *dst) throw(EFile)
		{
			File s(src, Read), d(dst, Write|Creat);

			off_t len = 4096;
			char buf[len];
			off_t left = s.size();

			while (left > 0) {
				off_t n = left < len ? left : len;

				s.read(buf, n);
				d.write(buf, n);

				left -= n;
			}
		}

		static void rename(const char *oldp, const char *newp) throw(EFile)
		{
			if (::rename(oldp, newp)) {
				if (errno == EACCES || errno == EPERM) throw ENoAccess();
				if (errno == ENOENT || errno == ENOTDIR) throw ENotExists();
				if (errno == ENOSPC) throw ENoSpace();
				if (errno != EXDEV) 
					throw EInternal("File::rename: %d, %s", errno, strerror(errno));
				// sciezki pokazuja na rozne partycje
				copy(oldp, newp);
				remove(oldp);
			} 
		}

		static void remove(const char *path) throw(EFile)
		{
			if (::unlink(path)) {
				if (errno == EACCES || errno == EPERM) throw ENoAccess();
				if (errno == ENOENT || errno == ENOTDIR) throw ENotExists();
				throw EInternal("File::remove: %d, %s", errno, strerror(errno));
			}
		}

		static void truncate(const char *path, off_t off = 0) throw(EFile)
		{
			while (::truncate(path, off)) {
				if (errno == EINTR || errno == EAGAIN) continue;
				if (errno == EPERM || errno == EACCES || errno == EISDIR) throw ENoAccess();
				if (errno == ENOENT || errno == ENOTDIR) throw ENotExists();
				throw EInternal("File::truncate: %d, %s", errno, strerror(errno));
			}
		}

		static bool exists(const char *path) throw()
		{
			struct stat st;
			
			if (stat(path, &st)) {
				if (errno == ENOENT || errno == ENOTDIR) return false;
				throw EInternal("File::exists: %d, %s", errno, strerror(errno));
			}

			return true;
		}

		static void create(const char *path) throw(EFile)
		{
			if (File::exists(path)) throw EExists();
			File f(path, Creat|Write|Read);
		}

	private:
		File(const File &); /* non-copyable */
		
		int m_fd;
		Mode m_mode;
};

#endif

