#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "filesystem.h"
#include "util.h"

#define ADDR_SIZE 4
#define FS_METADATA_SECTOR 0
#define FILE_SECTOR_DATA_SIZE (SECTOR_SIZE - MAX_FILENAME - (4 * ADDR_SIZE))
#define DATA_SECTOR_DATA_SIZE (SECTOR_SIZE - ADDR_SIZE)
#define FILE_CURSOR 1
#define FILE_ADDR 0

typedef struct
{
	uint32_t first_free_sector_addr;
	uint32_t last_free_sector_addr;
	uint32_t first_file_sector_addr;
	uint32_t last_file_sector_addr;
} fs_metadata_t;

typedef struct
{
	char filename[MAX_FILENAME];
	uint32_t size;
	uint32_t next_file_sector_addr;
	uint32_t first_data_sector_addr;
	uint32_t last_data_sector_addr;
	char data[FILE_SECTOR_DATA_SIZE];
} file_sector_t;

typedef struct
{
	uint32_t next_data_sector_addr;
	char data[DATA_SECTOR_DATA_SIZE];
} data_sector_t;

typedef struct
{
	uint32_t next_free_sector_addr;
} free_sector_t;

int get_free_sector_addr()
{
	uint8_t fs_buffer[SECTOR_SIZE] = {0};
	hdd_read(FS_METADATA_SECTOR, fs_buffer);
	fs_metadata_t *fs_metadata = (fs_metadata_t *)fs_buffer;
	int free_addr = fs_metadata->first_free_sector_addr;
	// we dont have any free sector
	if (free_addr == 0)
		return 0;

	uint8_t free_sector_buffer[SECTOR_SIZE] = {0};
	hdd_read(free_addr, free_sector_buffer);
	free_sector_t *free_sector = (free_sector_t *)free_sector_buffer;

	// update next free sector in fs metadata
	fs_metadata->first_free_sector_addr = free_sector->next_free_sector_addr;
	hdd_write(FS_METADATA_SECTOR, fs_buffer);
	return free_addr;
}
/**
 * Naformatovanie disku.
 *
 * Zavola sa vzdy, ked sa vytvara novy obraz disku.
 */
void fs_format()
{
	// create a linked list of free sectors
	for (size_t i = 1; i < hdd_size() / SECTOR_SIZE; i++)
	{
		uint8_t free_buff[SECTOR_SIZE] = {0};
		free_sector_t *free_sector = (free_sector_t *)free_buff;
		if (i == hdd_size() / SECTOR_SIZE - 1)
		{
			free_sector->next_free_sector_addr = 0;
		}
		else
		{
			free_sector->next_free_sector_addr = i + 1;
		}
		hdd_write(i, free_buff);
	}

	// zero sector reserved for filesystem metadata
	uint8_t fs_buff[SECTOR_SIZE] = {0};
	fs_metadata_t *fs_metadata = (fs_metadata_t *)fs_buff;
	fs_metadata->first_file_sector_addr = 0;
	fs_metadata->last_file_sector_addr = 0;
	fs_metadata->first_free_sector_addr = 1;
	fs_metadata->last_free_sector_addr = hdd_size() / SECTOR_SIZE - 1;
	hdd_write(FS_METADATA_SECTOR, fs_buff);
}

/**
 * Vytvorenie suboru.
 *
 * Volanie vytvori v suborovom systeme na zadanej ceste novy subor a vrati
 * handle nan. Ak subor uz existoval, bude skrateny na prazdny. Pozicia v subore bude
 * nastavena na 0ty byte. Ak adresar, v ktorom subor ma byt ulozeny, neexistuje,
 * vrati NULL (sam nevytvara adresarovu strukturu, moze vytvarat iba subory).
 */

file_t *fs_creat(const char *path)
{
	if (strrchr(path, PATHSEP) != path || strlen(path) > MAX_PATH)
	{
		return NULL;
	}

	uint8_t fs_buffer[SECTOR_SIZE] = {0};
	hdd_read(FS_METADATA_SECTOR, fs_buffer);
	fs_metadata_t *fs_metadata = (fs_metadata_t *)fs_buffer;

	uint32_t file_addr = fs_metadata->first_file_sector_addr;

	while (1)
	{
		uint8_t file_buff[SECTOR_SIZE] = {0};
		hdd_read(file_addr, file_buff);
		file_sector_t *file = (file_sector_t *)file_buff;

		if (strncmp(path, file->filename, MAX_FILENAME) == 0)
		{
			// file with the same name exists
			if (file->first_data_sector_addr != 0)
			{
				// file used data sectors, we need to add them into list of free sectors
				uint32_t data_sector_addr = file->first_data_sector_addr;
				while (1)
				{
					uint8_t data_buff[SECTOR_SIZE] = {0};
					hdd_read(data_sector_addr, data_buff);
					data_sector_t *data_sector = (data_sector_t *)data_buff;

					uint8_t free_buff[SECTOR_SIZE] = {0};
					free_sector_t *free_sector = (free_sector_t *)free_buff;

					free_sector->next_free_sector_addr = data_sector->next_data_sector_addr;
					if (data_sector->next_data_sector_addr == 0)
					{
						// last data sector, link it with currect first free sector
						free_sector->next_free_sector_addr = fs_metadata->first_free_sector_addr;
						hdd_write(data_sector_addr, free_buff);
						break;
					}
					hdd_write(data_sector_addr, free_buff);

					data_sector_addr = data_sector->next_data_sector_addr;
				}

				// update address of first free sector in fs metadata
				fs_metadata->first_free_sector_addr = file->first_data_sector_addr;
				hdd_write(FS_METADATA_SECTOR, fs_buffer);
			}
			file->first_data_sector_addr = 0;
			file->last_data_sector_addr = 0;
			file->size = 0;
			hdd_write(file_addr, file_buff);
			return fs_open(path);
		}
		else if (file->next_file_sector_addr == 0)
		{
			// we did not find file with same name, so we need to create new file
			uint8_t new_file_buffer[SECTOR_SIZE] = {0};
			file_sector_t *new_file = (file_sector_t *)new_file_buffer;
			uint32_t new_file_addr = fs_metadata->first_free_sector_addr;
			strcpy(new_file->filename, path);
			new_file->size = 0;
			new_file->last_data_sector_addr = 0;
			new_file->first_data_sector_addr = 0;
			new_file->next_file_sector_addr = 0;

			if (fs_metadata->first_file_sector_addr == 0)
			{
				// file system does not contain any files
				fs_metadata->first_file_sector_addr = 1;
				fs_metadata->last_file_sector_addr = 1;
			}
			else
			{
				// add new created file to list of file sectors
				uint32_t last_file_addr = fs_metadata->last_file_sector_addr;
				uint8_t last_buff[SECTOR_SIZE] = {0};
				hdd_read(last_file_addr, last_buff);
				file_sector_t *last_file = (file_sector_t *)last_buff;

				last_file->next_file_sector_addr = new_file_addr;

				fs_metadata->last_file_sector_addr = new_file_addr;
				hdd_write(last_file_addr, last_buff);
			}

			uint8_t free_sector_buffer[SECTOR_SIZE] = {0};
			hdd_read(new_file_addr, free_sector_buffer);
			free_sector_t *free_sector = (free_sector_t *)free_sector_buffer;
			fs_metadata->first_free_sector_addr = free_sector->next_free_sector_addr;

			hdd_write(FS_METADATA_SECTOR, fs_buffer);
			hdd_write(new_file_addr, new_file_buffer);
			return fs_open(path);
		}
		file_addr = file->next_file_sector_addr;
	}

	return NULL;
}

/**
 * Otvorenie existujuceho suboru.
 *
 * Ak zadany subor existuje, funkcia ho otvori a vrati handle nan. Pozicia v
 * subore bude nastavena na 0-ty bajt. Ak subor neexistuje, vrati NULL.
 */
file_t *fs_open(const char *path)
{
	uint8_t fs_buffer[SECTOR_SIZE] = {0};
	hdd_read(FS_METADATA_SECTOR, fs_buffer);
	fs_metadata_t *fs_metadata = (fs_metadata_t *)fs_buffer;
	uint32_t file_sector_addr = fs_metadata->first_file_sector_addr;

	while (1)
	{
		uint8_t file_buffer[SECTOR_SIZE] = {0};
		hdd_read(file_sector_addr, file_buffer);
		file_sector_t *file = ((file_sector_t *)file_buffer);

		if (strncmp(file->filename, path, MAX_FILENAME) == 0)
		{
			// file exists
			file_t *fd = fd_alloc();
			fd->info[FILE_ADDR] = file_sector_addr;
			fd->info[FILE_CURSOR] = 0;
			fd->info[2] = 0;
			fd->info[3] = 0;
			return fd;
		}
		else if (file->next_file_sector_addr == 0)
		{
			// file does not exist
			return NULL;
		}
		file_sector_addr = file->next_file_sector_addr;
	}
}

/**
 * Zatvori otvoreny file handle.
 *
 * Funkcia zatvori handle, ktory bol vytvoreny pomocou volania 'open' alebo
 * 'creat' a uvolni prostriedky, ktore su s nim spojene. V pripade akehokolvek
 * zlyhania vrati FAIL, inak OK.
 */
int fs_close(file_t *fd)
{
	/* Uvolnime filedescriptor, aby sme neleakovali pamat */
	fd_free(fd);
	return OK;
}

/**
 * Odstrani subor na ceste 'path'.
 *
 * Ak zadana cesta existuje a je to subor, odstrani subor z disku; nemeni
 * adresarovu strukturu. V pripade chyby vracia FAIL, inak OK.
 */
int fs_unlink(const char *path)
{
	uint8_t buffer[SECTOR_SIZE] = {0};
	hdd_read(FS_METADATA_SECTOR, buffer);
	fs_metadata_t *fs_metadata = (fs_metadata_t *)buffer;

	uint32_t file_addr = fs_metadata->first_file_sector_addr;
	uint8_t file_buff[SECTOR_SIZE] = {0};

	while (1)
	{
		hdd_read(file_addr, file_buff);
		file_sector_t *file = (file_sector_t *)file_buff;

		if (strncmp(file->filename, path, MAX_FILENAME) == 0)
		{
			// add all used sectors to the linked list of free sectors
			uint8_t last_free_sector_buff[SECTOR_SIZE] = {0};
			hdd_read(fs_metadata->last_free_sector_addr, last_free_sector_buff);
			data_sector_t *last_free_sector = (data_sector_t *)last_free_sector_buff;
			last_free_sector->next_data_sector_addr = file_addr;
			hdd_write(fs_metadata->last_free_sector_addr, last_free_sector_buff);

			uint8_t removed_file_buff[SECTOR_SIZE] = {0};
			free_sector_t *removed_file = (free_sector_t *)removed_file_buff;
			removed_file->next_free_sector_addr = file->first_data_sector_addr;
			hdd_write(file_addr, removed_file_buff);

			if (file->first_data_sector_addr != 0)
			{
				// if file used data sectors
				uint32_t next_data_sector_addr = file->first_data_sector_addr;

				while (1)
				{
					uint8_t next_data_sector_buff[SECTOR_SIZE] = {0};
					hdd_read(next_data_sector_addr, next_data_sector_buff);
					data_sector_t *next_data_sector = (data_sector_t *)next_data_sector_buff;

					uint8_t free_sector_buff[SECTOR_SIZE] = {0};
					hdd_write(next_data_sector_addr, free_sector_buff);
					free_sector_t *free_sector = (free_sector_t *)free_sector_buff;
					free_sector->next_free_sector_addr = next_data_sector->next_data_sector_addr;

					if (next_data_sector->next_data_sector_addr == 0)
					{
						fs_metadata->last_free_sector_addr = next_data_sector_addr;
						hdd_write(FS_METADATA_SECTOR, buffer);
						break;
					}

					next_data_sector_addr = next_data_sector->next_data_sector_addr;
				}
			}
			return OK;
		}
		else if (file->next_file_sector_addr == 0)
		{
			break;
		}
		file_addr = file->next_file_sector_addr;
	}
	return FAIL;
}

/**
 * Premenuje/presunie polozku v suborovom systeme z 'oldpath' na 'newpath'.
 *
 * Po uspesnom vykonani tejto funkcie bude subor, ktory doteraz existoval na
 * 'oldpath' dostupny cez 'newpath' a 'oldpath' prestane existovat. Opat,
 * funkcia nemanipuluje s adresarovou strukturou (nevytvara nove adresare
 * z cesty newpath, okrem posledneho v pripade premenovania adresara).
 * V pripade zlyhania vracia FAIL, inak OK.
 */
int fs_rename(const char *oldpath, const char *newpath)
{
	uint8_t fs_buffer[SECTOR_SIZE] = {0};
	hdd_read(FS_METADATA_SECTOR, fs_buffer);
	fs_metadata_t *fs_metadata = (fs_metadata_t *)fs_buffer;
	uint32_t file_sector_addr = fs_metadata->first_file_sector_addr;

	uint8_t buffer[SECTOR_SIZE] = {0};
	while (1)
	{

		hdd_read(file_sector_addr, buffer);
		file_sector_t *file_sector = (file_sector_t *)buffer;

		if (strncmp(file_sector->filename, oldpath, MAX_FILENAME) == 0)
		{
			// file exists
			strncpy(file_sector->filename, newpath, MAX_FILENAME);
			hdd_write(file_sector_addr, buffer);

			return OK;
		}
		else if (file_sector->next_file_sector_addr == 0)
		{
			// file does not exist
			return FAIL;
		}

		file_sector_addr = file_sector->next_file_sector_addr;
	}
}

/**
 * Nacita z aktualnej pozicie vo 'fd' do bufferu 'bytes' najviac 'size' bajtov.
 *
 * Z aktualnej pozicie v subore precita funkcia najviac 'size' bajtov; na konci
 * suboru funkcia vracia 0. Po nacitani dat zodpovedajuco upravi poziciu v
 * subore. Vrati pocet precitanych bajtov z 'bytes', alebo FAIL v pripade
 * zlyhania. Existujuci subor prepise.
 */
int fs_read(file_t *fd, uint8_t *bytes, size_t size)
{
	uint32_t file_addr = fd->info[FILE_ADDR];
	uint32_t file_cursor = fd->info[FILE_CURSOR];

	uint8_t file_buffer[SECTOR_SIZE] = {0};
	hdd_read(file_addr, file_buffer);
	file_sector_t *file = (file_sector_t *)file_buffer;
	int bytes_read = 0;

	if (file_cursor > file->size)
		return 0;
	if (file_cursor + size > file->size)
		size = file->size - file_cursor;
	if (file_cursor < FILE_SECTOR_DATA_SIZE)
	{
		// we need to read from frist file data sector
		int amount_to_read = size;
		if (amount_to_read > FILE_SECTOR_DATA_SIZE)
			amount_to_read = FILE_SECTOR_DATA_SIZE - file_cursor;

		for (int i = 0; i < amount_to_read; i++)
		{
			bytes[bytes_read] = file->data[file_cursor + i];
			bytes_read++;
			size--;
		}
	}
	// We need to read from the file data sector
	if (size > 0)
	{
		// we also need to read from data sectors
		uint32_t data_sector_addr = file->first_data_sector_addr;
		if (data_sector_addr != 0)
		{
			// find a suitable data sector which matches cursor position
			int cursor_data_sector_order = (file_cursor - FILE_SECTOR_DATA_SIZE) / (DATA_SECTOR_DATA_SIZE) + 1;
			for (int i = 0; i < cursor_data_sector_order; i++)
			{
				uint8_t data_buffer[SECTOR_SIZE] = {0};
				hdd_read(data_sector_addr, data_buffer);
				data_sector_t *data_sector = (data_sector_t *)data_buffer;
				if (i != (cursor_data_sector_order - 1))
					data_sector_addr = data_sector->next_data_sector_addr;
			}

			while (size > 0)
			{
				// reead from data sectors of file
				uint8_t data_buffer[SECTOR_SIZE] = {0};
				hdd_read(data_sector_addr, data_buffer);
				data_sector_t *data_sector = (data_sector_t *)data_buffer;
				int relative_file_cursor = (file_cursor - FILE_SECTOR_DATA_SIZE) % DATA_SECTOR_DATA_SIZE;
				int amount_to_read = size;
				if (amount_to_read > DATA_SECTOR_DATA_SIZE)
					amount_to_read = DATA_SECTOR_DATA_SIZE;
				if ((relative_file_cursor + amount_to_read) > DATA_SECTOR_DATA_SIZE)
					amount_to_read = DATA_SECTOR_DATA_SIZE - relative_file_cursor;

				for (int i = 0; i < amount_to_read; i++)
				{
					bytes[bytes_read] = data_sector->data[relative_file_cursor + i];
					bytes_read++;
					file_cursor++;
					size--;
				}
				data_sector_addr = data_sector->next_data_sector_addr;
				if (data_sector_addr == 0)
					break;
			}
		}
	}

	fd->info[FILE_CURSOR] += bytes_read;
	return bytes_read;
}

/**
 * Zapise do 'fd' na aktualnu poziciu 'size' bajtov z 'bytes'.
 *
 * Na aktualnu poziciu v subore zapise 'size' bajtov z 'bytes'. Ak zapis
 * presahuje hranice suboru, subor sa zvacsi; ak to nie je mozne, zapise sa
 * maximalny mozny pocet bajtov. Po zapise korektne upravi aktualnu poziciu v
 * subore a vracia pocet zapisanych bajtov z 'bytes'.
 * V pripade zlyhania vrati FAIL.
 *
 * Write existujuci obsah suboru prepisuje, nevklada dovnutra nove data.
 * Write pre poziciu tesne za koncom existujucich dat zvacsi velkost suboru.
 */
int fs_write(file_t *fd, const uint8_t *bytes, size_t size)
{
	uint32_t file_addr = fd->info[FILE_ADDR];
	uint32_t file_cursor = fd->info[FILE_CURSOR];

	uint8_t buffer[SECTOR_SIZE] = {0};
	hdd_read(file_addr, buffer);
	file_sector_t *file = (file_sector_t *)buffer;

	if (size == 0)
		return 0;
	int size_increase = 0;
	int bytes_written = 0;

	if (file_cursor < FILE_SECTOR_DATA_SIZE)
	{
		// we need to start writing into the first file sector
		int amount_to_write = size;
		if (file_cursor + amount_to_write > FILE_SECTOR_DATA_SIZE)
			amount_to_write = FILE_SECTOR_DATA_SIZE - file_cursor;
		for (int i = 0; i < amount_to_write; i++)
		{
			file->data[file_cursor + i] = bytes[i];
			bytes_written++;
			if (file_cursor + i >= file->size)
			{
				size_increase++;
			}
		}
	}
	file_cursor += bytes_written;
	size -= bytes_written;
	if (size > 0)
	{
		// we also need to write into data sectors
		uint32_t data_sector_addr = file->first_data_sector_addr;
		if (data_sector_addr == 0)
		{
			// file does not have data sectors yet
			data_sector_addr = get_free_sector_addr();
			file->first_data_sector_addr = data_sector_addr;
		}
		else
		{
			// find a suitable data sector address which matches cursor position
			int cursor_data_sector_order = (file_cursor - FILE_SECTOR_DATA_SIZE) / (DATA_SECTOR_DATA_SIZE) + 1;
			for (int i = 0; i < cursor_data_sector_order; i++)
			{
				uint8_t data_buffer[SECTOR_SIZE] = {0};
				hdd_read(data_sector_addr, data_buffer);
				data_sector_t *data_sector = (data_sector_t *)data_buffer;
				if (i != (cursor_data_sector_order - 1))
					data_sector_addr = data_sector->next_data_sector_addr;
			}
		}

		// at this time we have found a correct data sector address, we can start writing
		while (size > 0)
		{
			uint8_t data_buffer[SECTOR_SIZE] = {0};
			hdd_read(data_sector_addr, data_buffer);
			data_sector_t *data_sector = (data_sector_t *)data_buffer;
			int relative_file_cursor = (file_cursor - FILE_SECTOR_DATA_SIZE) % DATA_SECTOR_DATA_SIZE;
			int amount_to_write = size;
			if (amount_to_write > DATA_SECTOR_DATA_SIZE)
				amount_to_write = DATA_SECTOR_DATA_SIZE;
			if ((relative_file_cursor + amount_to_write) > DATA_SECTOR_DATA_SIZE)
				amount_to_write = DATA_SECTOR_DATA_SIZE - relative_file_cursor;

			for (int i = 0; i < amount_to_write; i++)
			{
				data_sector->data[relative_file_cursor + i] = bytes[bytes_written];
				bytes_written++;
				file_cursor++;
				size--;
				if (file_cursor >= file->size)
				{
					size_increase++;
				}
			}

			hdd_write(data_sector_addr, data_buffer);
			data_sector_addr = data_sector->next_data_sector_addr;
			if (data_sector_addr == 0)
				data_sector_addr = get_free_sector_addr();
			// we do not have any free sector, stop writing and return amount of written bytes
			if (data_sector_addr == 0)
				break;
		}
	}

	fd->info[FILE_CURSOR] = file_cursor;
	file->size += size_increase;
	hdd_write(file_addr, buffer);
	return bytes_written;
}

/**
 * Zmeni aktualnu poziciu v subore na 'pos'-ty byte.
 *
 * Upravi aktualnu poziciu; ak je 'pos' mimo hranic suboru, vrati FAIL a pozicia
 * sa nezmeni, inac vracia OK.
 */
int fs_seek(file_t *fd, size_t pos)
{
	uint32_t file_addr = fd->info[FILE_ADDR];
	uint8_t file_buffer[SECTOR_SIZE] = {0};

	hdd_read(file_addr, file_buffer);
	file_sector_t *file = (file_sector_t *)file_buffer;

	if (pos >= file->size)
	{
		return FAIL;
	}

	fd->info[FILE_CURSOR] = pos;
	return OK;
}

/**
 * Vrati aktualnu poziciu v subore.
 */

size_t fs_tell(file_t *fd)
{
	return fd->info[FILE_CURSOR];
}

/**
 * Vrati informacie o 'path'.
 *
 * Funkcia vrati FAIL ak cesta neexistuje, alebo vyplni v strukture 'fs_stat'
 * polozky a vrati OK:
 *  - st_size: velkost suboru v byte-och
 *  - st_nlink: pocet hardlinkov na subor (ak neimplementujete hardlinky, tak 1)
 *  - st_type: hodnota podla makier v hlavickovom subore: STAT_TYPE_FILE,
 *  STAT_TYPE_DIR, STAT_TYPE_SYMLINK
 *
 */

int fs_stat(const char *path, struct fs_stat *fs_stat)
{

	uint8_t fs_buffer[SECTOR_SIZE] = {0};
	hdd_read(FS_METADATA_SECTOR, fs_buffer);
	fs_metadata_t *fs_metadata = (fs_metadata_t *)fs_buffer;

	uint32_t file_sector_addr = fs_metadata->first_file_sector_addr;

	uint8_t buffer[SECTOR_SIZE] = {0};
	while (1)
	{

		hdd_read(file_sector_addr, buffer);
		file_sector_t *file_sector = (file_sector_t *)buffer;

		if (strncmp(file_sector->filename, path, MAX_FILENAME) == 0)
		{
			// file exists
			fs_stat->st_size = file_sector->size;
			fs_stat->st_nlink = 1;
			fs_stat->st_type = STAT_TYPE_FILE;

			return OK;
		}
		else if (file_sector->next_file_sector_addr == 0)
		{
			// file does not exist
			return FAIL;
		}

		file_sector_addr = file_sector->next_file_sector_addr;
	}
	return OK;
}

/* Level 3 */
/**
 * Vytvori adresar 'path'.
 *
 * Ak cesta, v ktorej adresar ma byt, neexistuje, vrati FAIL (vytvara najviac
 * jeden adresar), pri korektnom vytvoreni OK.
 */
int fs_mkdir(const char *path) { return FAIL; }

/**
 * Odstrani adresar 'path'.
 *
 * Odstrani prazdny adresar, na ktory ukazuje 'path'; ak obsahuje subory, neexistuje alebo nie je
 * adresar, vrati FAIL; po uspesnom dokonceni vrati OK.
 */
int fs_rmdir(const char *path) { return FAIL; }

/**
 * Otvori adresar 'path' (na citanie poloziek)
 *
 * Vrati handle na otvoreny adresar s poziciou nastavenou na 0; alebo NULL v
 * pripade zlyhania.
 */
file_t *fs_opendir(const char *path) { return NULL; }

/**
 * Nacita nazov dalsej polozky z adresara.
 *
 * Do dodaneho buffera ulozi nazov polozky v adresari, posunie aktualnu
 * poziciu na dalsiu polozku a vrati OK.
 * V pripade problemu, alebo ak nasledujuca polozka neexistuje, vracia FAIL.
 * (V pripade jedneho suboru v adresari vracia FAIL az pri druhom volani.)
 */
int fs_readdir(file_t *dir, char *item) { return FAIL; }

/**
 * Zatvori otvoreny adresar.
 * V pripade neuspechu vrati FAIL, inak OK.
 */
int fs_closedir(file_t *dir) { return FAIL; }

/* Level 4 */
/**
 * Vytvori hardlink zo suboru 'path' na 'linkpath'.
 * V pripade neuspechu vrati FAIL, inak OK.
 */
int fs_link(const char *path, const char *linkpath) { return FAIL; }

/**
 * Vytvori symlink z 'path' na 'linkpath'.
 * V pripade neuspechu vrati FAIL, inak OK.
 */
int fs_symlink(const char *path, const char *linkpath) { return FAIL; }
