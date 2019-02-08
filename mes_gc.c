/* -*-comment-start: "//";comment-end:""-*-
 * GNU Mes --- Maxwell Equations of Software
 * Copyright © 2016,2017 Jan (janneke) Nieuwenhuizen <janneke@gnu.org>
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
#include <errno.h>
#include <strings.h>

#define TYPE(x) g_cells[x].type
#define NTYPE(x) g_news[x].type
#define VECTOR(x) g_cells[x].vector
#define NVECTOR(x) g_news[x].vector
#define LENGTH(x) g_cells[x].length
#define NLENGTH(x) g_news[x].length
#define CAR(x) g_cells[x].rac
#define NVALUE(x) g_news[x].value

size_t bytes_cells(size_t length);
int eputs(char const* s);
char *itoa (int number);
SCM error(SCM key, SCM x);
struct scm* cstring_to_symbol(char const *s);
SCM write_error_ (SCM x);
SCM gc_push_frame();
SCM gc_pop_frame();

struct scm* gc_up_arena()  ///((internal))
{
	long old_arena_bytes = (ARENA_SIZE + JAM_SIZE) * sizeof(struct scm);

	if(ARENA_SIZE >> 1 < MAX_ARENA_SIZE >> 2)
	{
		ARENA_SIZE <<= 1;
		JAM_SIZE <<= 1;
		GC_SAFETY <<= 1;
	}
	else
	{
		ARENA_SIZE = MAX_ARENA_SIZE - JAM_SIZE;
	}

	long arena_bytes = (ARENA_SIZE + JAM_SIZE) * sizeof(struct scm);
	char* p = realloc(g_cells - 1, arena_bytes + STACK_SIZE * sizeof(SCM));

	if(!p)
	{
		eputs("realloc failed, g_free=");
		eputs(itoa(g_free));
		eputs(":");
		eputs(itoa(ARENA_SIZE - g_free));
		eputs("\n");
		assert(0);
		exit(EXIT_FAILURE);
	}

	g_cells = (struct scm*)p;
	memcpy(p + arena_bytes, p + old_arena_bytes, STACK_SIZE * sizeof(SCM));
	g_cells++;
	return NULL;
}

void gc_flip()  ///((internal))
{
	if(g_debug > 2)
	{
		eputs(";;;   => jam[");
		eputs(itoa(g_free));
		eputs("]\n");
	}

	if(g_free > JAM_SIZE)
	{
		JAM_SIZE = g_free + g_free / 2;
	}

	memcpy(g_cells - 1, g_news - 1, (g_free + 2)*sizeof(struct scm));
}

struct scm* gc_copy(SCM old)  ///((internal))
{
	struct scm* o = Getstructscm2(old, g_cells);
	if(o->type == TBROKEN_HEART)
	{
		return o->car;
	}

	SCM new = g_free++;
	struct scm* n = Getstructscm2(new, g_news);
	*n = *o;

	if(n->type == TSTRUCT || n->type == TVECTOR)
	{
		n->vector = g_free;
		long i;

		for(i = 0; i < o->length; i++)
		{
			g_news[g_free + i] = g_cells[VECTOR(old) + i];
		}

		g_free = g_free + i;;
	}
	else if(NTYPE(new) == TBYTES)
	{
		char const *src = (char*)&g_cells[old].rdc;
		char *dest = (char*)&g_news[new].rdc;
		size_t length = NLENGTH(new);
		memcpy(dest, src, length + 1);
		g_free += bytes_cells(length) - 1;

		if(g_debug > 4)
		{
			eputs("gc copy bytes: ");
			eputs(src);
			eputs("\n");
			eputs("    length: ");
			eputs(itoa(LENGTH(old)));
			eputs("\n");
			eputs("    nlength: ");
			eputs(itoa(NLENGTH(new)));
			eputs("\n");
			eputs("        ==> ");
			eputs(dest);
			eputs("\n");
		}
	}

	TYPE(old) = TBROKEN_HEART;
	CAR(old) = new;
	return Getstructscm(new);
}

struct scm* gc_copy_new(struct scm* old)  ///((internal))
{
	if(old->type == TBROKEN_HEART)
	{
		return bad2good(old->car, g_cells);
	}

	SCM new = g_free++;
	struct scm* n = Getstructscm2(new, g_news);
	*n = *old;

	if(n->type == TSTRUCT || n->type == TVECTOR)
	{
		n->vector = g_free;
		long i;

		for(i = 0; i < old->length; i++)
		{
			g_news[g_free + i] = g_cells[old->vector + i];
		}

		g_free = g_free + i;;
	}
	else if(n->type == TBYTES)
	{
		char const *src = (char*) &old->cdr;
		char *dest = (char*)&n->cdr;
		size_t length = n->length;
		memcpy(dest, src, length + 1);
		g_free += bytes_cells(length) - 1;

		if(g_debug > 4)
		{
			eputs("gc copy bytes: ");
			eputs(src);
			eputs("\n");
			eputs("    length: ");
			eputs(itoa(old->length));
			eputs("\n");
			eputs("    nlength: ");
			eputs(itoa(n->length));
			eputs("\n");
			eputs("        ==> ");
			eputs(dest);
			eputs("\n");
		}
	}

	old->type = TBROKEN_HEART;
	old->car = good2bad(n, g_news);
	return n;
}

void gc_loop()  ///((internal))
{
	SCM scan = 1;
	struct scm* s = g_news + 1;
	struct scm* g = Getstructscm2(g_free, g_news);

        while(s < g)
	{
		if(s->type == TBROKEN_HEART)
		{
			error(cell_symbol_system_error, GetSCM2(cstring_to_symbol("gc"), g_cells));
		}

		if(s->type == TMACRO
		        || s->type == TPAIR
		        || s->type == TREF
		        || scan == 1 // null
		        || s->type == TVARIABLE)
		{
			g_news[scan].rac = GetSCM2(bad2good(gc_copy(g_news[scan].rac), g_news), g_news);
			//g_news[scan].rac = GetSCM2(gc_copy_new (bad2good(g_news[scan].car, g_news)), g_news);
		}

		if((s->type == TCLOSURE
		        || s->type == TCONTINUATION
		        || s->type == TKEYWORD
		        || s->type == TMACRO
		        || s->type == TPAIR
		        || s->type == TPORT
		        || s->type == TSPECIAL
		        || s->type == TSTRING
		        || s->type == TSYMBOL
		        || scan == 1 // null
		        || s->type == TVALUES)
		        && g_news[scan].cdr) // allow for 0 terminated list of symbols
		{
			g_news[scan].rdc = GetSCM2(bad2good(gc_copy(g_news[scan].rdc), g_news), g_news);
			//g_news[scan].rdc = GetSCM2(gc_copy_new(bad2good(g_news[scan].cdr, g_news)), g_news);
		}

		if(s->type == TBYTES)
		{
			//scan += bytes_cells(s->length) - 1;
			int b = bytes_cells(s->length) - 1;
			scan += b;
			s += b;
		}

		scan++;
		s++;
		g = Getstructscm2(g_free, g_news);
	}

	gc_flip();
}

struct scm* gc();
struct scm* gc_check()
{
	if(g_free + GC_SAFETY > ARENA_SIZE)
	{
		gc();
	}

	return Getstructscm(cell_unspecified);
}

struct scm* gc_init_news()  ///((internal))
{
	g_news = g_cells + g_free;
	NTYPE(0) = TVECTOR;
	NLENGTH(0) = 1000;
	NVECTOR(0) = 0;
	g_news++;
	NTYPE(0) = TCHAR;
	NVALUE(0) = 'n';
	return NULL;
}

void gc_()  ///((internal))
{
	gc_init_news();

	if(g_debug == 2)
	{
		eputs(".");
	}

	if(g_debug > 2)
	{
		eputs(";;; gc[");
		eputs(itoa(g_free));
		eputs(":");
		eputs(itoa(ARENA_SIZE - g_free));
		eputs("]...");
	}

	g_free = 1;
	if(ARENA_SIZE < MAX_ARENA_SIZE && g_news > 0)
	{
		if(g_debug == 2)
		{
			eputs("+");
		}

		if(g_debug > 2)
		{
			eputs(" up[");
			eputs(itoa((unsigned long)g_cells));
			eputs(",");
			eputs(itoa((unsigned long)g_news));
			eputs(":");
			eputs(itoa(ARENA_SIZE));
			eputs(",");
			eputs(itoa(MAX_ARENA_SIZE));
			eputs("]...");
		}

		gc_up_arena();
	}

	for(long i = g_free; i < g_symbol_max; i++)
	{
		//gc_copy(i);
		gc_copy_new(Getstructscm2(i, g_cells));
	}

	// g_symbols = GetSCM(gc_copy(g_symbols));
	// g_macros = GetSCM(gc_copy(g_macros));
	// g_ports = GetSCM(gc_copy(g_ports));
	// m0 = GetSCM(gc_copy(m0));

	//g_symbols = GetSCM2(gc_copy_new(Getstructscm2(g_symbols, g_cells)), Getstructscm2(g_symbols, g_cells));
	// g_macros = GetSCM2(gc_copy_new(Getstructscm2(g_macros, g_cells)), Getstructscm2(g_macros, g_cells));
	// g_ports = GetSCM2(gc_copy_new(Getstructscm2(g_ports, g_cells)), Getstructscm2(g_ports, g_cells));
	// m0 = GetSCM2(gc_copy_new(Getstructscm2(m0, g_cells)), g_cells);

	g_symbols = good2bad (gc_copy_new(Getstructscm2(g_symbols, g_cells)), g_news);
	g_macros = good2bad (gc_copy_new(Getstructscm2(g_macros, g_cells)), g_news);
	g_ports = good2bad (gc_copy_new(Getstructscm2(g_ports, g_cells)), g_news);
	m0 = good2bad (gc_copy_new(Getstructscm2(m0, g_cells)), g_news);

	for(long i = g_stack; i < STACK_SIZE; i++)
	{
		g_stack_array[i] = GetSCM(gc_copy(g_stack_array[i]));
		//g_stack_array[i] = good2bad(gc_copy_new(bad2good(g_stack_array[i], g_cells)), g_cells);
	}

	gc_loop();
}

struct scm* gc()
{
	if(g_debug > 4)
	{
		eputs("symbols: ");
		write_error_(g_symbols);
		eputs("\n");
		eputs("R0: ");
		write_error_(r0);
		eputs("\n");
	}

	gc_push_frame();
	gc_();
	gc_pop_frame();

	if(g_debug > 4)
	{
		eputs("symbols: ");
		write_error_(g_symbols);
		eputs("\n");
		eputs("R0: ");
		write_error_(r0);
		eputs("\n");
	}
	return Getstructscm(cell_unspecified);
}
