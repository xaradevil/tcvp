package TCVP;

my @filters;
my %filters;
my %packet;
my %event;
my %etypes;
my $probe;
my $flush;
my @module;
my $dtype;

sub init {
    undef @filters;
    undef %filters;
    undef %packet;
    undef %event;
    undef %etypes;
    undef $probe;
    undef $flush;
    undef @module;
    undef $dtype;
}

sub tc2module {
    $_ = shift;
    if (/filter\s+"([^\"]+)"\s+"([^\"]+)"\s+"([^\"]+)"\s+(\w+)/) {
	push @filters, [$1, $2, $3, $4];
	if (not exists $filters{$1}) {
	    my $n = '_tcvp_' . TC2::symbol($1, 'new');
	    $filters{$1} = [$4, $n];
	    TC2::tc2_export($1, 'new', $n);
	}
	$dtype = 'tcvp_pipe_t';
    } elsif (/module\s+"([^\"]+)"\s+(\w+)(?:\s+(\w+))?/) {
	my $n = '_tcvp_' . TC2::symbol($1, 'new');
	@module = ($1, $2, $3, $n);
	$dtype = 'tcvp_module_t';
	TC2::tc2_export($1, 'new', $n);
    } elsif (/event\s+(status|control|timer)
	      \s+(\w+) # name
	      \s+(\w+) # handler
	      (?:\s+(\w+)\s+(\w+)\s+(\w+))?/x) {
	$event{$2} = [$1, $3, $4, $5, $6];
	$etypes{$1} = 1 if $3 ne 'NULL';
	TC2::tc2_import('Eventq', 'new');
	TC2::tc2_import('Eventq', 'send');
	TC2::tc2_import('Eventq', 'recv');
	TC2::tc2_import('Eventq', 'attach');
	TC2::tc2_import('Eventq', 'delete');
	if($4){
	    TC2::tc2_import('tcvp/event', 'register');
	} else {
	    TC2::tc2_import('tcvp/event', 'get');
	}
	TC2::tc2_import('tcvp/event', 'send');
    } elsif (/packet\s+(\w+)\s+(\w+)/) {
	$packet{$1} = $2;
    } elsif (/flush\s+(\w+)/) {
	$flush = $1;
    } elsif (/probe\s+(\w+)/) {
	$probe = $1;
    } else {
	return 0;
    }
    return 1;
}

sub cmod {
    my($fh) = @_;

    if ($dtype) {
	print $fh "typedef struct tcvp_wrapper {\n";
	print $fh "    $dtype p;\n";
	print $fh "    tcconf_section_t *conf;\n";
	if (%event) {
	    print $fh <<END_C;
    eventq_t qr;
    pthread_t eth;
END_C
	}
	print $fh "} tcvp_wrapper_t;\n\n";
    }

    print $fh "int $_;\n" for keys %event;

    if (%event) {
	print $fh <<END_C;
static void *
tcvp_event_loop(void *p)
{
    tcvp_wrapper_t *tp = p;
    int run = 1;

    while(run){
	tcvp_event_t *te = eventq_recv(tp->qr);
END_C
	my $else;
	while(my($name, $e) = each %event){
	    if($$e[1] && $$e[1] ne 'NULL'){
		print $fh <<END_C;
	${else}if(te->type == $name){
	    $$e[1](&tp->p, te);
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

    if (%filters) {
	print $fh <<END_C;
static void
tcvp_filter_free(void *p)
{
    tcvp_pipe_t *tp = p;
    if(tp->private)
	tcfree(tp->private);
}

static int
tcvp_filter_probe(tcvp_pipe_t *p, packet_t *pk, stream_t *s)
{
    int ps;
    p->format = *s;
END_C
	if($probe){
	    print $fh <<END_C;
    ps = $probe(p, pk, s);
    if(ps == PROBE_OK)
	ps = p->next->probe(p->next, pk, &p->format);
END_C
	}
	print $fh <<END_C;
    return ps;
}

static int
tcvp_filter_packet(tcvp_pipe_t *p, packet_t *pk)
{
END_C
	my $else;
	while(my($type, $func) = each %packet){
	    print $fh <<END_C;
    ${else}if(pk->type == TCVP_PKT_TYPE_$type){
	return $func(p, pk);
    }
END_C
	    $else = 'else ';
	}
	print $fh <<END_C;
    ${else}\{
	return p->next->input(p->next, pk);
    }
}

static int
tcvp_filter_flush(tcvp_pipe_t *p, int drop)
{
END_C
	if ($flush) {
	    print $fh "    $flush(p, drop);\n";
	}
	print $fh <<END_C;
    return p->next->flush(p->next, drop);
}

END_C
	for (values %filters) {
	    print $fh <<END_C;
extern tcvp_pipe_t *
$$_[1](stream_t *s, tcconf_section_t *cs, tcvp_timer_t *t, muxed_stream_t *ms)
{
    tcvp_wrapper_t *p = tcallocdz(sizeof(*p), NULL, tcvp_filter_free);
    p->p.input = tcvp_filter_packet;
    p->p.probe = tcvp_filter_probe;
    p->p.flush = tcvp_filter_flush;

    if($$_[0](&p->p, s, cs, t, ms)){
	tcfree(p);
	return NULL;
    }
    return &p->p;
}

END_C
	}
    }

    if (@module) {
	print $fh <<END_C;
static void
tcvp_module_free(void *p)
{
    tcvp_wrapper_t *tp = p;
END_C
	if (%event) {
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
_tcvp_module_init(tcvp_module_t *m)
{
    tcvp_wrapper_t *p = (tcvp_wrapper_t *) m;
    int r = 0;
END_C

	if (%event) {
	    print $fh <<END_C;
    char *qname, *qn;
    tcconf_getvalue(p->conf, "qname", "%s", &qname);
    qn = malloc(strlen(qname) + 10);
    p->qr = eventq_new(tcref);
END_C

	    for (keys %etypes) {
		print $fh <<END_C;
    sprintf(qn, "%s/$_", qname);
    eventq_attach(p->qr, qn, EVENTQ_RECV);
END_C
	    }
	    print $fh <<END_C;
    pthread_create(&p->eth, NULL, tcvp_event_loop, p);
    free(qn);
    free(qname);
END_C
	}
	print $fh "    r = $module[2](m);\n" if $module[2];
	print $fh <<END_C;
    return r;
}

extern tcvp_module_t *
$module[3](tcconf_section_t *cs)
{
    tcvp_wrapper_t *p = tcallocdz(sizeof(*p), NULL, tcvp_module_free);
    p->p.init = _tcvp_module_init;

    if($module[1](&p->p, cs)){
	tcfree(p);
	return NULL;
    }
    p->conf = tcref(cs);
    return &p->p;
}
END_C
    }
}

sub hmod {
    my($fh) = @_;

    print $fh "#include <tcalloc.h>\n";
    print $fh "#include <tcconf.h>\n";

    while (my($name, $e) = each %event) {
	print $fh "extern int $name;\n";
	print $fh "extern int $$e[1]($dtype *, tcvp_event_t *);\n"
	  if $$e[1] ne 'NULL';
	print $fh "extern tcvp_alloc_event_t $$e[2];\n"
	  if $$e[2] && $$e[2] ne 'NULL';
	if ($$e[3] && $$e[3] ne 'NULL') {
	    print $fh "extern tcvp_serialize_event_t $$e[3];\n";
	    print $fh "extern tcvp_deserialize_event_t $$e[4];\n";
	}
    }

    if(@module){
	print $fh
	  "extern int $module[1](tcvp_module_t *, tcconf_section_t *);\n";
    }

    for(values %filters){
	print $fh <<END_C;
extern int $$_[0](tcvp_pipe_t *, stream_t *, tcconf_section_t *,
		 tcvp_timer_t *, muxed_stream_t *);
END_C
    }
    print $fh "extern int $_(tcvp_pipe_t *, packet_t *);\n" for values %packet;
    print $fh "extern int $probe(tcvp_pipe_t *, packet_t *, stream_t *);\n"
      if $probe;
    print $fh "extern int $flush(tcvp_pipe_t *, int);\n" if $flush;
}

sub postinit {
    my($fh) = @_;

    while (my($name, $e) = each %event) {
	if (not defined $$e[2]) {
	    print $fh qq/    $name = tcvp_event_get("$name");\n/;
	} else {
	    print $fh qq/    $name = tcvp_event_register("$name", $$e[2], $$e[3], $$e[4]);\n/;
	}
    }
}

sub unload {
}

1;
