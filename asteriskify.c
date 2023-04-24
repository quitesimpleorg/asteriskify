/*
 * Copyright (c) 2022 Albert Schwarzkopf <dev at quitesimple dot org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <termios.h>
#include <unistd.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>
#include <sys/prctl.h>

#define PWBUF_SIZE 256

#define MODE_ECHO 0
#define MODE_STARS 1

struct termios saved_termios;
uint8_t *pwbuf = NULL;
size_t pwbufsize  = 0;
size_t pwindex = 0;
int current_mode = MODE_ECHO;

void enter_raw_mode()
{
	struct termios raw = saved_termios;
	raw.c_lflag &= ~(ECHO | ICANON);
	if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0)
	{
		fprintf(stderr, "Failed tcsetattr\n");
		exit(EXIT_FAILURE);
	}
}

void setup_console()
{
	if(tcgetattr(STDIN_FILENO, &saved_termios) != 0)
	{
		fprintf(stderr, "Failed tcgetattr\n");
		exit(EXIT_FAILURE);
	}
	enter_raw_mode();
}

void restore_console()
{
	if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_termios) != 0)
	{
		fprintf(stderr, "Failed tcsetattr\n");
		exit(EXIT_FAILURE);
	}
}


void allocate_pw_buf()
{
	pwbuf = calloc(PWBUF_SIZE, sizeof(uint8_t));
	if(pwbuf == NULL)
	{
		fprintf(stderr, "Failed to allocate memory\n");
		exit(EXIT_FAILURE);
	}
	int lock = mlock(pwbuf, PWBUF_SIZE);
	if(lock != 0)
	{
		fprintf(stderr, "Failed to mlock buffer\n");
		exit(EXIT_FAILURE);
	}
	pwbufsize = PWBUF_SIZE;
}

void grow_pw_buf()
{
	size_t newsize = pwbufsize + PWBUF_SIZE;
	uint8_t *newbuf = calloc(newsize, sizeof(uint8_t));
	if(newbuf == NULL)
	{
		fprintf(stderr, "Failed to allocate memory\n");
		exit(EXIT_FAILURE);
	}
	int lock = mlock(newbuf, newsize);
	if(lock != 0)
	{
		fprintf(stderr, "Failed to mlock buffer\n");
		exit(EXIT_FAILURE);
	}
	memcpy(newbuf, pwbuf, pwbufsize);
	explicit_bzero(pwbuf, pwbufsize);
	pwbuf = newbuf;
	pwbufsize = newsize;
}

void exit_handler()
{
	explicit_bzero(pwbuf, pwbufsize);
	explicit_bzero(&pwbufsize, sizeof(pwbufsize));
	explicit_bzero(&pwindex, sizeof(pwbufsize));

	restore_console();
}

void clear_term_line()
{
	const char *clearcmd = "\33[2K\r";
	write(STDERR_FILENO, clearcmd, strlen(clearcmd));
}

void print_password()
{
	clear_term_line();
	char *prompt = "Password: ";
	write(STDERR_FILENO, prompt, strlen(prompt));
	if(current_mode == MODE_ECHO)
	{
		write(STDERR_FILENO, pwbuf, pwindex);
	}
	if(current_mode == MODE_STARS)
	{
		for(size_t i = 0; i < pwindex; i++)
		{
			uint8_t n = pwbuf[i];
			/* Skip utf-8 byte 2-4, we won't print an asterisk for those*/
			if((n >>6) == 0b10)
			{
				continue;
			}
			char mask = '*';
			write(STDERR_FILENO, &mask, 1);
		}
	}
	fsync(STDERR_FILENO);
}

void switch_mode()
{
	current_mode = ( current_mode + 1 ) % 2;
}

int main()
{
	if(atexit(exit_handler) != 0)
	{
		fprintf(stderr, "Failed to register exit handler\n");
		exit(EXIT_FAILURE);
	}

	if(prctl(PR_SET_DUMPABLE, 0) != 0)
	{
		fprintf(stderr, "Failed to make process not dumpable\n");
		exit(EXIT_FAILURE);
	}

	setup_console();

	allocate_pw_buf();

	print_password();

	uint8_t c;
	while (read(STDIN_FILENO, &c, 1) == 1)
	{
		if(iscntrl(c))
		{
			if(c == '\t')
			{
				switch_mode();
				print_password();
			}
			if(c == 27) /* Escape sequence */
			{
				int remaining = 0;
				int ret = ioctl (STDIN_FILENO,FIONREAD,&remaining);
				if(ret != 0)
				{
					fprintf(stderr, "ioctl() failed\n");
					exit(EXIT_FAILURE);
				}
				char tmp;
				/* Just consume them, we don't care */
				while(remaining--)
				{
					read(STDIN_FILENO, &tmp, 1);
				}
			}
			if(c == 18)  /* Control + R, device control 2. or "reveal" for us :-) */
			{
				if(pwindex > 0)
				{
					char c = '\b';
					write(STDERR_FILENO, &c, 1);
					write(STDERR_FILENO, &pwbuf[pwindex-1], 1);
					fsync(STDERR_FILENO);
				}
			}
			if(c == 127) /* Backspace */
			{
				if(pwindex > 0)
				{
					--pwindex;
				}
				print_password();
				continue;
			}
			if(c == '\n')
			{
				break;
			}
			continue;
		}
		if(pwindex == pwbufsize)
		{
			grow_pw_buf();
		}

		pwbuf[pwindex] = c;
		++pwindex;

		print_password();
	}
	clear_term_line();

	fsync(STDERR_FILENO);
	write(STDOUT_FILENO, pwbuf, pwindex);
	fsync(STDOUT_FILENO);

	return 0;
}
