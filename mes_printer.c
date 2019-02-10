/* -*-comment-start: "//";comment-end:""-*-
 * GNU Mes --- Maxwell Equations of Software
 * Copyright © 2016,2017,2018 Jan (janneke) Nieuwenhuizen <janneke@gnu.org>
 * Copyright © 2019 Jeremiah Orians
 *
 * This file is part of GNU Mes.
 *
 * GNU Mes is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or (at
 * your option) any later version.
 *
 * GNU Mes is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Mes.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "mes.h"
#include "mes_constants.h"

// CONSTANT STRUCT_PRINTER 1
#define STRUCT_PRINTER 1

SCM cons (SCM x, SCM y);
SCM apply(SCM f, SCM x);
struct scm* struct_ref_(SCM x, long i);
SCM builtin_p (SCM x);
struct scm* fdisplay_(SCM, int, int);
int fdputs(char const* s, int fd);
int fdputc(int c, int fd);
char *itoa (int number);

/* Imported Functions */
void raw_print(char* s, int fd);
void fd_print(char* s, int f);
char* char_lookup(int c, int type);

/* Globals */
int g_depth;

struct scm* display_helper(SCM x, int cont, char* sep, int fd, int write_p)
{
	struct scm* y = Getstructscm2(x, g_cells);
	fdputs(sep, fd);

	if(g_depth == 0)
	{
		return good2bad(Getstructscm2(cell_unspecified, g_cells), g_cells);
	}

	g_depth = g_depth - 1;
	int t = y->type;

	if(t == TCHAR)
	{
		if(!write_p)
		{
			fdputc(y->value, fd);
		}
		else
		{
			fdputc('#', fd);
			fd_print(char_lookup(y->value, TRUE), fd);
		}
	}
	else if(t == TCLOSURE)
	{
		fdputs("#<closure ", fd);
		struct scm* name = bad2good(bad2good(bad2good(bad2good(y->cdr, g_cells)->car, g_cells)->cdr, g_cells)->car, g_cells);
		SCM args = bad2good(bad2good(bad2good(y->cdr, g_cells)->cdr, g_cells)->car, g_cells)->rac;
		display_helper(name->rac, 0, "", fd, 0);
		fdputc(' ', fd);
		display_helper(args, 0, "", fd, 0);
		fdputc('>', fd);
	}
	else if(t == TMACRO)
	{
		fdputs("#<macro ", fd);
		display_helper(y->rdc, cont, "", fd, 0);
		fdputc('>', fd);
	}
	else if(t == TVARIABLE)
	{
		fdputs("#<variable ", fd);
		display_helper(bad2good(y->car, g_cells)->rac, cont, "", fd, 0);
		fdputc('>', fd);
	}
	else if(t == TNUMBER)
	{
		fdputs(itoa(y->value), fd);
	}
	else if(t == TPAIR)
	{
		if(!cont) fdputc('(', fd);

		if(y->rac == cell_circular && bad2good(y->cdr, g_cells)->rac != cell_closure)
		{
			fdputs("(*circ* . ", fd);
			int i = 0;
			y = bad2good(y->cdr, g_cells);

			while(GetSCM2(y, g_cells) != cell_nil && i++ < 10)
			{
				fdisplay_(bad2good(y->car, g_cells)->rac, fd, write_p);
				fdputc(' ', fd);
				y = bad2good(y->cdr, g_cells);
			}

			fdputs(" ...)", fd);
		}
		else
		{
			if(GetSCM2(y, g_cells) != cell_nil)
			{
				fdisplay_(y->rac, fd, write_p);
			}

			if(bad2good(y->cdr, g_cells)->type == TPAIR)
			{
				display_helper(y->rdc, 1, " ", fd, write_p);
			}
			else if(y->rdc != cell_nil)
			{
				if(bad2good(y->cdr, g_cells)->type != TPAIR)
				{
					fdputs(" . ", fd);
				}

				fdisplay_(y->rdc, fd, write_p);
			}
		}

		if(!cont) fdputc(')', fd);
	}
	else if(t == TPORT)
	{
		fdputs("#<port ", fd);
		fdputs(itoa(y->rac), fd);
		fdputc(' ', fd);
		char *s = (char*)bad2good(bad2good(y->cdr, g_cells)->cdr, g_cells)->rdc;
		raw_print(s, fd);
		fdputs("\">", fd);
	}
	else if(t == TKEYWORD)
	{
		fdputs("#:", fd);
		char *s = (char*)&bad2good(y->cdr, g_cells)->rdc;
		raw_print(s, fd);
	}
	else if(t == TSTRING)
	{
		if(write_p) fdputc('"', fd);
		char *s = (char*)&bad2good(y->cdr, g_cells)->rdc;
		fd_print(s, fd);
		if(write_p) fdputc('"', fd);
	}
	else if(t == TSPECIAL)
	{
		char *s = (char*)&bad2good(y->cdr, g_cells)->rdc;
		raw_print(s, fd);
	}
	else if(t == TSYMBOL)
	{
		char *s = (char*)&bad2good(y->cdr, g_cells)->rdc;
		raw_print(s, fd);
	}
	else if(t == TREF)
	{
		fdisplay_(y->ref, fd, write_p);
	}
	else if(t == TSTRUCT)
	{
		//SCM printer = STRUCT (x) + 1;
		struct scm* printer = bad2good(struct_ref_(x, STRUCT_PRINTER), g_cells);

		if(printer->type == TREF)
		{
			printer = bad2good(printer->car, g_cells);
		}

		if(printer->type == TCLOSURE || builtin_p(GetSCM2(printer, g_cells)) == cell_t)
		{
			apply(GetSCM2(printer, g_cells), cons(x, cell_nil));
		}
		else
		{
			fdputs("#<", fd);
			fdisplay_(y->struc, fd, write_p);

			long size = y->length;

			for(long i = 2; i < size; i++)
			{
				fdputc(' ', fd);
				fdisplay_(y->struc + i, fd, write_p);
			}

			fdputc('>', fd);
		}
	}
	else if(t == TVECTOR)
	{
		fdputs("#( ", fd);

		for(long i = 1; i < y->length; i++)
		{
			fdisplay_(y->vector + i, fd, write_p);
		}

		fdputc(')', fd);
	}
	else
	{
		fdputs("<", fd);
		fdputs(itoa(t), fd);
		fdputs(":", fd);
		fdputs(itoa(x), fd);
		fdputs(">", fd);
	}

	return 0;
}

struct scm* display_(SCM x)
{
	g_depth = 5;
	return display_helper(x, 0, "", __stdout, 0);
}

struct scm* display_error_(SCM x)
{
	g_depth = 5;
	return display_helper(x, 0, "", __stderr, 0);
}

struct scm* display_port_(SCM x, SCM p)
{
	struct scm* p2 = Getstructscm2(p, g_cells);
	assert(p2->type == TNUMBER);
	return fdisplay_(x, p2->value, 0);
}

struct scm* write_(SCM x)
{
	g_depth = 5;
	return display_helper(x, 0, "", __stdout, 1);
}

struct scm* write_error_(SCM x)
{
	g_depth = 5;
	return display_helper(x, 0, "", __stderr, 1);
}

struct scm* write_port_(SCM x, SCM p)
{
	struct scm* p2 = Getstructscm2(p, g_cells);
	assert(p2->type == TNUMBER);
	return fdisplay_(x, p2->value, 1);
}

struct scm* fdisplay_(SCM x, int fd, int write_p)  ///((internal))
{
	g_depth = 5;
	return display_helper(x, 0, "", fd, write_p);
}

struct scm* frame_printer(SCM frame)
{
	fdputs("#<", __stdout);
	display_(GetSCM2(bad2good(struct_ref_(frame, 2), g_cells), g_cells));
	fdputs(" procedure: ", __stdout);
	display_(GetSCM2(bad2good(struct_ref_(frame, 3), g_cells), g_cells));
	fdputc('>', __stdout);
	return good2bad(Getstructscm2(cell_unspecified, g_cells), g_cells);
}