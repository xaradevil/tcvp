# Copyright (C) 2003-2005  Michael Ahlberg, Måns Rullgård

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
	TC2::tc2_require('tcvp/module');
    } elsif ($module and /alias\s+"([^\"]+)"/) {
	TC2::tc2_export($1, 'new', $$module{wnew});
    } elsif ($module and $$module{type} eq 'filter' and
	     /(probe|flush)\s+(\w+)/) {
	$$module{$1} = $2;
    } elsif ($module and $$module{type} eq 'filter' and
	     /packet\s+(\w+)\s+(\w+)/) {
	$$module{packet}{$1} = $2;
    } elsif ($module and /(new|init)\s+(\w+)/) {
	$$module{$1} = $2;
    } elsif ($module and
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
	TC2::tc2_import('tcvp/event', 'format');
	TC2::tc2_import('tcvp/event', 'get_qname');
	TC2::tc2_import('tcvp/event', 'loop');
	TC2::tc2_require("tcvp/events/$2");
    } elsif (not $module and
	     /event\s+(\w+)(?:\s+(\w+)(?:\s+(\w+)\s+(\w+))?)?/) {
	$events{$1} = { alloc => $2,
			ser => $3,
			deser => $4 };
	TC2::tc2_import('tcvp/event', $2? 'register': 'get');
	TC2::tc2_import('tcvp/event', 'format');
	if ($2) {
	    my($alloc, $ser, $deser);
	    if($2 eq 'auto'){
		my $pfx = lc $1 . "_auto_";
		$events{$1}{alloc} = $alloc = $pfx . 'alloc';
		$events{$1}{ser} = $ser = $pfx . 'serialize';
		$events{$1}{deser} = $deser = $pfx . 'deserialize';
		$events{$1}{auto} = 1;
	    } else {
		$alloc = $2 eq 'NULL'? '@tcvp/events:alloc': $2;
		$ser = $3 eq 'NULL'? '@tcvp/events:serialize': $3;
		$deser = $4 eq 'NULL'? '@tcvp/events:deserialize': $4;
	    }
	    TC2::tc2_export("tcvp/events/$1", "alloc", $alloc);
	    TC2::tc2_export("tcvp/events/$1", "serialize", $ser);
	    TC2::tc2_export("tcvp/events/$1", "deserialize", $deser);
	} else {
	    TC2::tc2_require("tcvp/events/$1");
	}
    } elsif (/^}/) {
	undef $module;
    } else {
	return 0;
    }

    return 1;
}

my %etypes = ( i => { type => 'int32_t',
		      size => 'sizeof(%s)',
		      get => '%s' },
	       I => { type => 'int64_t',
		      size => 'sizeof(%s)',
		      get => '%s' },
	       u => { type => 'uint32_t',
		      size => 'sizeof(%s)',
		      get => '%s' },
	       U => { type => 'uint64_t',
		      size => 'sizeof(%s)',
		      get => '%s' },
	       f => { type => 'double',
		      size => 'sizeof(%s)',
		      get => '%s' },
	       c => { type => 'int',
		      size => '1',
		      get => '%s' },
	       's' => { type => 'char *',
			size => '(strlen(%s) + 1)',
			get => 'strdup(%s)',
			free => 'free(%s)' },
	       p => { type => 'void *',
		      get => '%s' });

sub interface {
    my $int = shift;
    $_ = shift;

    if(/event\s+(\w+)\s*((\w+%([IiUufcsp](\[(.*?)\])?)\s*)*)/){
	my($n, $d) = ($1, $2);
	my $et = lc $n . '_event';
	if ($d) {
	    my $fmt;
	    push @{$$int{include}}, "typedef struct $et \{\n";
	    push @{$$int{include}}, "    int type;\n";
	    while ($d =~ /(\w+)%([IiUufcsp](\[(.*?)\])?)/g) {
		my($f, $t, $t1) = ($1, $2, $4);
		my $type = $t1 || $etypes{$t}{type};
		push @{$$int{include}}, "    $type $f;\n";
		my $fc = substr $t, 0, 1;
		push @{$events{$n}{fields}}, [$type, $f, $fc];
		$fmt .= $fc;
	    }
	    push @{$$int{include}}, "} ${et}_t;\n";
	    $events{$n}{format} = $fmt;
	} else {
	    push @{$$int{include}}, "typedef tcvp_event_t ${et}_t;\n";
	    $events{$n}{format} = '';
	}
	$events{$n}{type} = $et;
	unshift @{$$int{inherit}}, 'tcvp/events';
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
	print $fh "    tcvp_pipe_t filter;\n" if $$_{type} eq 'filter';
	print $fh "    tcvp_module_t module;\n";
	print $fh "    tcconf_section_t *conf;\n";
	if ($$_{events}) {
	    print $fh <<END_C;
    eventq_t qr;
    pthread_t eth;
END_C
	}
	print $fh "} $$_{wtype}_t;\n\n";

	if ($$_{events}) {
	    my $nh = keys(%{$$_{events}}) + 1;
	    print $fh "static tcvp_event_type_handler_t $$_{w}event_handlers[$nh];\n";
	}

	print $fh <<END_C;
static void
$$_{w}free(void *p)
{
    $$_{wtype}_t *tp = p;
END_C
	print $fh <<END_C if $$_{events};
    tcvp_event_send(tp->qr, -1);
    pthread_join(tp->eth, NULL);
    eventq_delete(tp->qr);
END_C
	print $fh <<END_C;
    tcfree(tp->module.private);
    tcfree(tp->conf);
}
END_C

	print $fh <<END_C;
static int
$$_{w}init(tcvp_module_t *m)
{
    int r = 0;
END_C

	if ($$_{events}) {
	    print $fh <<END_C;
    $$_{wtype}_t *p = ($$_{wtype}_t *) m;
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
    tcvp_event_loop(p->qr, $$_{w}event_handlers, &p->module, &p->eth);
    free(qn);
    free(qname);
END_C
	}
	print $fh "    r = $$_{init}(m);\n" if $$_{init};
	print $fh <<END_C;
    return r;
}
END_C

	if ($$_{type} eq 'filter') {
	    print $fh <<END_C;
static int
$$_{w}probe(tcvp_pipe_t *p, tcvp_data_packet_t *pk, stream_t *s)
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
$$_{w}packet(tcvp_pipe_t *p, tcvp_packet_t *pk)
{
END_C
	    my $else;
	    while (my($type, $func) = each %{$$_{packet}}) {
		my $ltype = lc $type;
		print $fh <<END_C;
    ${else}if(pk->type == TCVP_PKT_TYPE_$type){
	return $func(p, &pk->$ltype);
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

    p->module.init = $$_{w}init;

    p->filter.format = *s;
    p->filter.input = $$_{w}packet;
    p->filter.probe = $$_{w}probe;
    p->filter.flush = $$_{w}flush;
END_C
	    print $fh <<END_C if $$_{new};
    if($$_{new}(&p->filter, s, cs, t, ms)){
	tcfree(p);
	return NULL;
    }
END_C
	    print $fh <<END_C;
    p->module.private = p->filter.private;
    if($$_{w}init(&p->module)){
        tcfree(p);
        return NULL;
    }

    p->conf = tcref(cs);
    return &p->filter;
}
END_C
	} elsif ($$_{type} eq 'module') {
	    print $fh <<END_C;
extern tcvp_module_t *
$$_{w}new(tcconf_section_t *cs)
{
    $$_{wtype}_t *p = tcallocdz(sizeof(*p), NULL, $$_{w}free);
    p->module.init = $$_{w}init;

    if($$_{new}(&p->module, cs)){
	tcfree(p);
	return NULL;
    }
    p->conf = tcref(cs);
    return &p->module;
}
END_C
	}
    }

    print $fh "#include <tcendian.h>\n";

    while (my($name, $e) = each %events) {
	next if !$$e{auto};
	die "can't create funcs for $name ($1)\n" if $$e{format} =~ /([fp])/;
	my $free = 'NULL';
	if ($$e{format} =~ /s/) {
	    $free = $$e{type} . '_auto_free';
	    print $fh <<END_C;
static void
$free(void *p)
{
    $$e{type}_t *e = p;
END_C
	    for (@{$$e{fields}}) {
		my($t, $n, $c) = @$_;
		printf $fh "    $etypes{$c}{free};\n", "e->$n"
		  if exists $etypes{$c}{free};
	    }
	    print $fh "}\n\n";
	}
	print $fh <<END_C;
extern void *
$$e{alloc}(int type, va_list args)
{
    $$e{type}_t *e = tcvp_event_alloc(type, sizeof(*e), $free);
END_C
	for (@{$$e{fields}}) {
	    my($t, $n, $c) = @$_;
	    printf $fh "    e->$n = $etypes{$c}{get};\n", "va_arg(args, $t)";
	}
	print $fh "    return e;\n";
	print $fh "}\n\n";

	print $fh <<END_C;
extern u_char *
$$e{ser}(char *name, void *event, int *size)
{
    $$e{type}_t *e = event;
    u_char *sb, *p;
    int s = strlen(name) + 1;
END_C
	printf $fh "    s += %s;\n", join '+',
	  map sprintf($etypes{$$_[2]}{size}, "e->$$_[1]"), @{$$e{fields}};
	print $fh "    p = sb = malloc(s);\n";
	print $fh qq/    p += sprintf(p, "$name") + 1;\n/;
	for (@{$$e{fields}}) {
	    my($t, $n, $c) = @$_;
	    if ($c eq 'i' or $c eq 'u') {
		print $fh "    st_unaligned32(htob_32(e->$n), p);\n";
		print $fh "    p += 4;\n"
	    } elsif ($c eq 'I' or $c eq 'U') {
		print $fh "    st_unaligned64(htob_64(e->$n), p);\n";
		print $fh "    p += 8;\n"
	    } elsif ($c eq 'c') {
		print $fh "    *p++ = e->$n;\n";
	    } elsif ($c eq 's') {
		print $fh qq/    p += sprintf(p, "%s", e->$n) + 1;\n/;
	    }
	}
	print $fh "    *size = s;\n";
	print $fh "    return sb;\n";
	print $fh "}\n\n";

	print $fh <<END_C;
extern void *
$$e{deser}(int type, u_char *_event, int _size)
{
    u_char *_p = memchr(_event, 0, _size);
END_C
	print $fh "    u_char *_n;\n" if $$e{format} =~ /s/;
	print $fh "    $$_[0] $$_[1];\n" for (@{$$e{fields}});
	print $fh "    _size -= ++_p - _event;\n";
	for (@{$$e{fields}}) {
	    my($t, $n, $c) = @$_;
	    if ($c eq 'i' or $c eq 'u') {
		print $fh <<END_C;
    if(_size < 4)
	return NULL;
    $n = htob_32(unaligned32(_p));
    _p += 4;
    _size -= 4;
END_C
	    } elsif ($c eq 'I' or $c eq 'U') {
		print $fh <<END_C;
    if(_size < 8)
	return NULL;
    $n = htob_64(unaligned64(_p));
    _p += 8;
    _size -= 8;
END_C
	    } elsif ($c eq 'c') {
		print $fh <<END_C;
    if(_size < 1)
	return NULL;
    $n = *_p++;
    _size--;
END_C
	    } elsif ($c eq 's') {
		print $fh <<END_C;
    _n = memchr(_p, 0, _size);
    if(!_n)
	return NULL;
    $n = _p;
    _p = _n + 1;
END_C
	    }
	}
	printf $fh "    return tcvp_event_new(type, %s);\n",
	  join ',', map $$_[1], @{$$e{fields}};
	print $fh "}\n";
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
	    print $fh "extern int $func(tcvp_pipe_t *, tcvp_@{[lc $type]}_packet_t *);\n"
	      while ($type, $func) = each %{$$_{packet}};
	    print $fh "extern int $$_{probe}(tcvp_pipe_t *, tcvp_data_packet_t *, stream_t *);\n" if $$_{probe};
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
	    my $fmt = defined $$e{format}? qq/"$$e{format}"/: '""';
	    print $fh qq/    $name = tcvp_event_register("$name", $$e{alloc}, $$e{ser}, $$e{deser}, $fmt);\n/;
	}
    }

    for (values %modules) {
	if ($$_{events}) {
	    my $ehi = 0;
	    while (my($name, $e) = each %{$$_{events}}) {
		next unless $$e{handler};
		print $fh "    $$_{w}event_handlers[$ehi].type = $name;\n";
		print $fh "    $$_{w}event_handlers[$ehi].handler = $$e{handler};\n";
		$ehi++;
	    }
	}
    }
}

sub unload {
}

1;
