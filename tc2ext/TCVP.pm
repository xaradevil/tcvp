# Copyright (C) 2003-2004  Michael Ahlberg, Måns Rullgård

# Permission is hereby granted, free of charge, to any person obtaining
# a copy of this software and associated documentation files (the
# "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, sublicense, and/or sell copies of the Software, and to
# permit persons to whom the Software is furnished to do so, subject to
# the following conditions:

# The above copyright notice and this permission notice shall be
# included in all copies or substantial portions of the Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
# LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
# OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
# WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

package TCVP;

my %modules;
my $module;
my %events;

sub init {
    undef %modules;
    undef $module;
    undef %events;
}

sub tc2module {
    $_ = shift;
    return 0 if /{$/ and $module;

    if (/(filter|module)\s+"([^\"]+)"\s*{/) {
	my $w = TC2::symbol("_tcvp_$2_");
	my @tags;
	if($1 eq 'filter'){
	    @tags = (type => 'filter',
		     dtype => 'tcvp_pipe_t');
	} else {
	    @tags = (type => 'module',
		     dtype => 'tcvp_module_t');
	}
	$modules{$2} = { wnew => $w . 'new',
			 wtype => $w . 'wrapper',
			 w => $w,
			 @tags };
	$module = $modules{$2};
	TC2::tc2_export($2, 'new', $$module{wnew});
    } elsif ($module and /alias\s+"([^\"]+)"/) {
	TC2::tc2_export($1, 'new', $$module{wnew});
    } elsif ($module and $$module{type} eq 'filter' and
	     /(new|probe|flush)\s+(\w+)/) {
	$$module{$1} = $2;
    } elsif ($module and $$module{type} eq 'filter' and
	     /packet\s+(\w+)\s+(\w+)/) {
	$$module{packet}{$1} = $2;
    } elsif ($module and $$module{type} eq 'module' and
	     /(new|init)\s+(\w+)/) {
	$$module{$1} = $2;
    } elsif ($module and $$module{type} eq 'module' and
	     /event\s+(status|control|timer)\s+(\w+)(?:\s+(\w+))?/) {
	$$module{events}{$2} = { handler => $3 };
	$$module{etypes}{$1} = 1;
	$events{$2}{use} = 1;
	TC2::tc2_import('Eventq', 'new');
	TC2::tc2_import('Eventq', 'send');
	TC2::tc2_import('Eventq', 'recv');
	TC2::tc2_import('Eventq', 'attach');
	TC2::tc2_import('Eventq', 'delete');
	TC2::tc2_import('tcvp/event', 'get');
	TC2::tc2_import('tcvp/event', 'send');
	TC2::tc2_import('tcvp/event', 'get_qname');
    } elsif (not $module and /event\s+(\w+)(?:\s+(\w+)\s+(\w+)\s+(\w+))?/) {
	$events{$1} = { alloc => $2,
			ser => $3,
			deser => $4 };
	TC2::tc2_import('tcvp/event', $2? 'register': 'get');
    } elsif (/^}/) {
	undef $module;
    } else {
	return 0;
    }

    return 1;
}

sub cmod {
    my($fh) = @_;

    print $fh "int $_;\n" for keys %events;

    for (values %modules) {
	print $fh "typedef struct $$_{wtype} {\n";
	print $fh "    $$_{dtype} p;\n";
	print $fh "    tcconf_section_t *conf;\n";
	if ($$_{events}) {
	    print $fh <<END_C;
    eventq_t qr;
    pthread_t eth;
END_C
	}
	print $fh "} $$_{wtype}_t;\n\n";

	if ($$_{events}) {
	    print $fh <<END_C;
static void *
$$_{w}event_loop(void *p)
{
    $$_{wtype}_t *tp = p;
    int run = 1;

    while(run){
	tcvp_event_t *te = eventq_recv(tp->qr);
END_C
	    my $else;
	    while (my($name, $e) = each %{$$_{events}}) {
		if ($$e{handler}) {
		    print $fh <<END_C;
	${else}if(te->type == $name){
	    $$e{handler}(&tp->p, te);
	}
END_C
		    $else = 'else ';
		}
	    }
	    print $fh <<END_C;
	${else}if(te->type == -1){
	    run = 0;
	}
	tcfree(te);
    }

    return NULL;
}
END_C
	}

	if ($$_{type} eq 'filter') {
	    print $fh <<END_C;
static void
$$_{w}free(void *p)
{
    tcvp_pipe_t *tp = p;
    if(tp->private)
	tcfree(tp->private);
}

static int
$$_{w}probe(tcvp_pipe_t *p, packet_t *pk, stream_t *s)
{
    int ps = PROBE_OK;
    p->format = *s;
END_C
	    print $fh <<END_C if $$_{probe};
    if(pk)
	tcref(pk);
    ps = $$_{probe}(p, pk, s);
END_C
	    print $fh <<END_C;
    if(ps == PROBE_OK && p->next)
	ps = p->next->probe(p->next, pk, &p->format);
    return ps;
}

static int
$$_{w}packet(tcvp_pipe_t *p, packet_t *pk)
{
END_C
	    my $else;
	    while (my($type, $func) = each %{$$_{packet}}) {
		print $fh <<END_C;
    ${else}if(pk->type == TCVP_PKT_TYPE_$type){
	return $func(p, pk);
    }
END_C
		$else = 'else ';
	    }
	    print $fh <<END_C;
    ${else}if(p->next){
	return p->next->input(p->next, pk);
    }
    tcfree(pk);
    return 0;
}

static int
$$_{w}flush(tcvp_pipe_t *p, int drop)
{
END_C
	    print $fh "    $$_{flush}(p, drop);\n" if $$_{flush};
	    print $fh <<END_C;
    return p->next? p->next->flush(p->next, drop): 0;
}

END_C
	    print $fh <<END_C;
extern tcvp_pipe_t *
$$_{w}new(stream_t *s, tcconf_section_t *cs, tcvp_timer_t *t,
	  muxed_stream_t *ms)
{
    $$_{wtype}_t *p = tcallocdz(sizeof(*p), NULL, $$_{w}free);
    p->p.format = *s;
    p->p.input = $$_{w}packet;
    p->p.probe = $$_{w}probe;
    p->p.flush = $$_{w}flush;
END_C
	    print $fh <<END_C if $$_{new};
    if($$_{new}(&p->p, s, cs, t, ms)){
	tcfree(p);
	return NULL;
    }
END_C
	    print $fh <<END_C;
    return &p->p;
}

END_C
	} elsif ($$_{type} eq 'module') {
	    print $fh <<END_C;
static void
$$_{w}free(void *p)
{
    $$_{wtype}_t *tp = p;
END_C
	    if ($$_{events}) {
		print $fh <<END_C;
    tcvp_event_send(tp->qr, -1);
    pthread_join(tp->eth, NULL);
    eventq_delete(tp->qr);
END_C
	    }
	    print $fh <<END_C;
    if(tp->p.private)
	tcfree(tp->p.private);
    tcfree(tp->conf);
}

static int
$$_{w}init(tcvp_module_t *m)
{
    $$_{wtype}_t *p = ($$_{wtype}_t *) m;
    int r = 0;
END_C

	    if ($$_{events}) {
		print $fh <<END_C;
    char *qname, *qn;
    qname = tcvp_event_get_qname(p->conf);
    qn = malloc(strlen(qname) + 10);
    p->qr = eventq_new(tcref);
END_C

		for (keys %{$$_{etypes}}) {
		    print $fh <<END_C;
    sprintf(qn, "%s/$_", qname);
    eventq_attach(p->qr, qn, EVENTQ_RECV);
END_C
		}
		print $fh <<END_C;
    pthread_create(&p->eth, NULL, $$_{w}event_loop, p);
    free(qn);
    free(qname);
END_C
	    }
	    print $fh "    r = $$_{init}(m);\n" if $$_{init};
	    print $fh <<END_C;
    return r;
}

extern tcvp_module_t *
$$_{w}new(tcconf_section_t *cs)
{
    $$_{wtype}_t *p = tcallocdz(sizeof(*p), NULL, $$_{w}free);
    p->p.init = $$_{w}init;

    if($$_{new}(&p->p, cs)){
	tcfree(p);
	return NULL;
    }
    p->conf = tcref(cs);
    return &p->p;
}
END_C
	}
    }
}

sub hmod {
    my($fh) = @_;

    print $fh "#include <tcalloc.h>\n";
    print $fh "#include <tcconf.h>\n";

    while (my($name, $e) = each %events) {
	print $fh "extern int $name;\n";
	print $fh "extern tcvp_alloc_event_t $$e{alloc};\n"
	  if $$e{alloc} and $$e{alloc} ne 'NULL';
	if ($$e{ser} and $$e{ser} ne 'NULL') {
	    print $fh "extern tcvp_serialize_event_t $$e{ser};\n";
	    print $fh "extern tcvp_deserialize_event_t $$e{deser};\n";
	}
    }

    for (values %modules) {
	if ($$_{type} eq 'module') {
	    print $fh
	      "extern int $$_{new}(tcvp_module_t *, tcconf_section_t *);\n"
		if $$_{new};
	    print $fh "extern int $$_{init}(tcvp_module_t *);\n" if $$_{init};
	    for my $e (values %{$$_{events}}) {
		print $fh "extern int $$e{handler}(tcvp_module_t *, tcvp_event_t *);\n" if $$e{handler};
	    }
	} elsif ($$_{type} eq 'filter') {
	    print $fh <<END_C if $$_{new};
extern int $$_{new}(tcvp_pipe_t *, stream_t *, tcconf_section_t *,
		    tcvp_timer_t *, muxed_stream_t *);
END_C
	    print $fh "extern int $_(tcvp_pipe_t *, packet_t *);\n"
	      for values %{$$_{packet}};
	    print $fh "extern int $$_{probe}(tcvp_pipe_t *, packet_t *, stream_t *);\n" if $$_{probe};
	    print $fh "extern int $$_{flush}(tcvp_pipe_t *, int);\n"
	      if $$_{flush};
	}
    }
}

sub postinit {
    my($fh) = @_;

    while (my($name, $e) = each %events) {
	if (not defined $$e{alloc}) {
	    print $fh qq/    $name = tcvp_event_get("$name");\n/;
	} else {
	    print $fh qq/    $name = tcvp_event_register("$name", $$e{alloc}, $$e{ser}, $$e{deser});\n/;
	}
    }
}

sub unload {
}

1;
