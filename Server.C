//
// Server.C -- News server class
//
// Copyright 2003-2004 Michael Sweet
// Copyright 2002 Greg Ercolano
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public Licensse as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
//

#include "Server.H"
#include <dirent.h>


// Convenience macros...
#define ISIT(x)		if (!strcasecmp(cmd, x))
#define ISHEAD(a)	(strncasecmp(head, (a), strlen(a))==0)

// SENDS CRLF TERMINATED MESSAGE TO REMOTE
int Server::Send(const char *msg)
{
    write(msgsock, msg, strlen(msg));
    write(msgsock, "\r\n", 2);
    G_conf.LogMessage(L_DEBUG, "SEND: %s", msg);
    return (0);
}

// SEE IF GROUPNAME IS ON THIS SERVER
int Server::ValidGroup(const char *groupname)
{
    // DISALLOW UNIX PATHNAME LOOPHOLES
    if ( strstr(groupname, "..") )
	{ errmsg = "illegal groupname"; return(-1); }

    // CHECK FOR INVALID CHARS IN GROUPNAME
    for ( int t=0; groupname[t]; t++ )
	if ( ( groupname[t] >= 'a' && groupname[t] <= 'z' ) ||	// alphalower
	     ( groupname[t] >= 'A' && groupname[t] <= 'Z' ) ||	// alphaupper
	     ( groupname[t] >= '0' && groupname[t] <= '9' ) ||	// numeric
	     groupname[t] == '.' )                       	// dot
	    { continue; }
	else
	    { errmsg = "illegal chars in groupname"; return(-1); }

    string dirname;
    dirname = G_conf.SpoolDir();
    dirname.append("/");
    dirname.append(groupname);

    struct stat sbuf;
    if ( stat(dirname.c_str(), &sbuf) == -1 )
	{ errmsg = "group does not exist"; return(-1); }

    return(0);
}

// FIND GROUP, UPDATE START/END/TOTAL INFO
int Server::NewGroup(const char *the_group)
{
    if ( ValidGroup(the_group) < 0 ) 
	{ return(-1); }

    if ( group.Load(the_group) < 0 )
	{ errmsg = group.Errmsg(); return(-1); }

    return(0);
}

// BREAK A LONG LINE INTO SMALLER PIECES
//    Useful for breaking eg. a big Bcc: list into separate lines.
//
void BreakLineToFP(FILE *fp, 
		   const char *prefix, 		// prefix, eg. "Bcc: "
		   const char *line, 		// long line, eg. "a,b,c,d"
		   const char *trail,		// trailing chars, eg. "\n"
		   const char *brk)		// list of chars to break on
						// eg. ","
{
    string addr;
    const char *ss = line;
    for (; 1; ++ss )
    {
	// break character?
	if ( *ss == 0 || strchr(brk, *ss) )
	{
	    if ( addr != "" )
		{ fprintf(fp, "%s%s%s", prefix, addr.c_str(), trail); }
	    addr = "";
	    if ( *ss == 0 ) return;
	}
	else
	    { addr += *ss; }
    }
}

void AllGroups(vector<string>& groupnames, const char *subdir)
{
    DIR *dir;
    struct dirent *dent;
    struct stat fileinfo;
    string dirname, filename, newsubdir;
    char groupname[LINE_LEN], *ptr;


    dirname = G_conf.SpoolDir();

    if (subdir)
	dirname = dirname + "/" + subdir;

    if ((dir = opendir(dirname.c_str())) != NULL)
    {
	while ((dent = readdir(dir)) != NULL)
	{
            // Skip dot files...
            if (dent->d_name[0] == '.')
		continue;

            // See if the file is a directory...
	    filename = dirname + "/" + dent->d_name;
	    if (stat(filename.c_str(), &fileinfo))
		continue;

            if (!S_ISDIR(fileinfo.st_mode))
		continue;

            // It is a directory, see if it is a group...
	    filename += "/.info";
	    if (!stat(filename.c_str(), &fileinfo))
	    {
		 // Yes, add the group...
		 if (subdir)
		 {
		     // Use the subdirectory for the group name, and map / to . in
		     // the group name...
		     snprintf(groupname, sizeof(groupname), "%s.%s", subdir,
	        	      dent->d_name);

        	     for (ptr = strchr(groupname, '/'); ptr; ptr = strchr(ptr + 1, '/'))
			 *ptr = '.';
        	 }
		 else
		 {
		     // TODO: add strlcpy() and emulation as needed...
		     strncpy(groupname, dent->d_name, sizeof(groupname) - 1);
		     groupname[sizeof(groupname) - 1] = '\0';
		 }

		 groupnames.push_back(groupname);
            }

            // Now recurse into the subdirectory...
	    if (subdir)
	        newsubdir = string(subdir) + "/" + dent->d_name;
	    else
	        newsubdir = dent->d_name;

	    AllGroups(groupnames, newsubdir.c_str());
	}

	closedir(dir);
    }
}

// HANDLE SIGALRM
//    alarm() used to timeout inactive child servers.
//
static void sigalrm_handler(int)
{
//    cerr << "ALARM -- connection timed out. (child exiting)" << endl;
    exit(1);
}

// HANDLE COMMANDS FROM REMOTE
int Server::CommandLoop(const char *overview[])
{
    const char *remoteip_str = GetRemoteIPStr();
    char s[LINE_LEN+1],
	 cmd[LINE_LEN+1],
	 arg1[LINE_LEN+1],
	 arg2[LINE_LEN+1],
	 reply[LINE_LEN];

    Send("200 newsd news server ready - posting ok");

    // HANDLE ALARM -- timeout the connection if no data transacted
    signal(SIGALRM, sigalrm_handler);

    while ( 1 )
    {
        int total = 0;
	char *ss = s;
	int found = 0;

	// RESET TIMEOUT ALARM
	if ( G_conf.Timeout() )
	    { alarm(G_conf.Timeout()); }

	// READ A CRLF-TERMINATED (OR LF-TERMINATED) LINE
	for ( ; read(msgsock, ss, 1) == 1; ss++, total++ )
	{
	    if ( *ss == '\n' || total >= (LINE_LEN-2) )
	        { *ss = 0; found = 1; break; }

	    if ( *ss == '\r' )
	    {
	        read(msgsock, ss, 1);		// skip \n
		*ss = 0; 
		found = 1;
		break;
	    }
	}

	if ( ! found )
	    { break; }

	G_conf.LogMessage(L_INFO, "GOT: %s", s);

	arg1[0] = arg2[0] = 0;
	if ( sscanf(s, "%s%s%s", cmd, arg1, arg2) < 1 )
	    { continue; }

	ISIT("CHECK")			// TRANSPORT EXTENSION -- RFC 2980
	{
	    Send("400 not accepting articles - we are not a news feed");
	    continue;
	}

	ISIT("TAKETHIS")		// TRANSPORT EXTENSION -- RFC 2980
	{
	    Send("400 not accepting articles - we are not a news feed");
	    continue;
	}

	ISIT("MODE")			// TRANSPORT EXTENSION -- RFC 2980
	{
	    if ( strcasecmp(arg1, "stream") == 0 )
	    {
		Send("500 Streaming not implemented on this server");
		continue;
	    }

	    // NEWS READER EXTENSION -- RFC 2980
	    if ( strcasecmp(arg1, "reader") == 0 )
	    {
		// Send("201 erco's newsd server ready (no posting)");
		Send("200 erco's newsd server ready (posting ok)");
		continue;
	    }

	    Send("500 What?");		// inn/nnrp/commands.c:CMDmode() - erco
	    continue;
	}

	ISIT("LIST")
	{
	    if ( strcasecmp(arg1, "EXTENSIONS") == 0 )	// INTERNET DRAFT (S.Barber)
	    {
		Send("202 Extensions supported:\r\n"
		     "LISTGROUP\r\n"
		     "MODE\r\n"
		     "XREPLIC\r\n"
		     "XOVER\r\n"
		     "DATE\r\n"
		     ".");
		continue;
	    }

	    if ( strcasecmp(arg1, "ACTIVE") == 0 ||	// NEWS READER EXTENSION -- RFC 2980
	         arg1[0] == 0 )				// RFC 977
	    {
	        // "LIST ACTIVE rush.*"
		if ( strcasecmp(cmd, "LIST") == 0 && 
		     strcasecmp(arg1, "ACTIVE") == 0 &&
		     arg2[0] != 0 )
		{
		    Send("501 LIST ACTIVE <wildmat>: wildmats not supported");	// TBD
		    continue;
		}

		// "LIST" or "LIST ACTIVE"
		Send("215 list of newsgroups follows");
		vector<string> groupnames;
		AllGroups(groupnames, NULL);
		for ( unsigned t=0; t<groupnames.size(); t++ )
		{
		    Group tgroup;
		    if ( tgroup.Load(groupnames[t].c_str()) < 0 )
		        { continue; }
		    sprintf(reply, "%s %d %d %c",
			(const char*)tgroup.Name(),
			(int)tgroup.Total(),
			(int)tgroup.Start(),
			(char)(tgroup.PostOK() ? 'y' : 'n'));
		    Send(reply);
		}
		Send(".");
		continue;
	    }

	    if ( strcasecmp(arg1, "ACTIVE.TIMES")==0 )	// NEWS READER EXTENSION -- RFC 2980
	    {
		Send("215 information follows");

		vector<string> groupnames;
		AllGroups(groupnames, NULL);
		for ( unsigned t=0; t<groupnames.size(); t++ )
		{
		    Group tgroup;
		    if ( tgroup.Load(groupnames[t].c_str()) < 0 )
		        { continue; }
		    sprintf(reply, "%s %ld %s", 
		        (const char*)tgroup.Name(),
		        (long)tgroup.Ctime(),
			(const char*)tgroup.Creator());
		    Send(reply);
		}
		Send(".");
		continue;
	    }

	    if ( strcasecmp(arg1, "DISTRIBUTIONS")==0 )	// NEWS READER EXTENSION -- RFC 2980
	    {
		// TODO
		Send("503 Not implemented on this server");
		continue;
	    }

	    if ( strcasecmp(arg1, "DISTRIB.PATS")==0 )	// NEWS READER EXTENSION -- RFC 2980
	    {
		// TODO
		Send("503 Not implemented on this server");
		continue;
	    }

	    if ( strcasecmp(arg1, "NEWSGROUPS")==0 )	// NEWS READER EXTENSION -- RFC 2980
	    {
		Send("215 information follows");
		vector<string> groupnames;
		AllGroups(groupnames, NULL);
		for ( unsigned t=0; t<groupnames.size(); t++ )
		{
		    Group tgroup;
		    if ( tgroup.Load(groupnames[t].c_str()) < 0 )
		        { continue; }
		    sprintf(reply, "%s %s",
		        (const char*)tgroup.Name(),
		        (const char*)tgroup.Description());
		    Send(reply);
		}
		Send(".");
		continue;
	    }

	    if ( strcasecmp(arg1, "OVERVIEW.FMT")==0 )	// NEWS READER EXTENSION -- RFC 2980
	    {
		Send("215 information follows");
		for ( int t=0; overview[t]; t++ )
		    { Send(overview[t]); }
		Send(".");
		continue;
	    }

	    if ( strcasecmp(arg1, "SUBSCRIPTIONS")==0 )	// NEWS READER EXTENSION -- RFC 2980
	    {
		Send("215 information follows");
		Send("rush.general");			// HACK: TBD
		Send(".");
		continue;
	    }

	    Send("501 Syntax error");
	    continue;
	}

	ISIT("LISTGROUP")				// TRANSPORT EXTENSION -- RFC 2980
	{
	    Group restore = group;

	    if ( arg1[0] )
	    {
		if ( group.Load(arg1) < 0 )
		{
		    sprintf(reply, "411 No such newsgroup: %s", 
		        (const char*)group.Errmsg());
		    Send(reply);
		    group = restore;
		    continue;
		}
	    }

	    if ( arg1[0] == 0 && ! group.IsValid() )
	    {
		Send("412 Not currently in newsgroup");
		continue;
	    }

	    // RFC 2980: SET CURRENT ARTICLE TO FIRST
	    article.Load(group.Start());

	    Send("211 list of article numbers follow");
	    for ( unsigned long t = group.Start(); t <= group.End(); t++ )
		{ sprintf(reply, "%lu", t); Send(reply); }
	    Send(".");
	    continue;
	}

	ISIT("XREPLIC")					// TRANSPORT EXTENSION -- RFC 2980
	{
	    Send("437 'xreplic' not implemented on this server");
	    continue;
	}

	ISIT("XOVER")					// NEWS READER EXTENSION -- RFC 2980
	{
	    // From RFC2970 for XOVER:
	    //
	    //   Each line of output will be formatted with the article number,
	    //   followed by each of the headers in the overview database or the
	    //   article itself (when the data is not available in the overview
	    //   database) for that article separated by a tab character.  The
	    //   sequence of fields must be in this order: subject, author, date,
	    //   message-id, references, byte count, and line count.  Other optional
	    //   fields may follow line count.  Other optional fields may follow line
	    //   count.  These fields are specified by examining the response to the
	    //   LIST OVERVIEW.FMT command.  Where no data exists, a null field must
	    //   be provided (i.e. the output will have two tab characters adjacent to
	    //   each other).  Servers should not output fields for articles that have
	    //   been removed since the XOVER database was created.
	    //
	    if ( ! group.IsValid() )
	    {
		Send("412 Not in a newsgroup");
		continue;
	    }

	    unsigned long sarticle = group.Start(),
		  earticle = group.End();

	    // HANDLE OPTIONAL RANGE
	    if ( arg1[0] )
	    {
	        if ( sscanf(arg1, "%lu-%lu", &sarticle, &earticle) == 2 )
		    { }
		else if ( sscanf(arg1, "%lu-", &sarticle) == 1 )
		    { earticle = group.End(); }
		else
		    { earticle = sarticle; }
	    }

	    // SANITIZE ARTICLE NUMBERS
	    if ( sarticle < group.Start() ) { sarticle = group.Start(); }
	    if ( sarticle > group.End() )   { sarticle = group.End(); }
	    if ( earticle < group.Start() ) { earticle = group.Start(); }
	    if ( earticle > group.End() )   { earticle = group.End(); }
	    if ( sarticle > earticle )      { sarticle = earticle; }

	    Send("224 overview follows");

	    for ( unsigned long t=sarticle; t<=earticle; t++ )
	    {
	        // LOAD EACH ARTICLE
	        Article a;
		if ( a.Load(group.Name(), t) < 0 )
		{
//		    cerr << "    DEBUG: ERROR: " << a.Errmsg() << endl;
		    continue;
		}

		string reply = a.Overview(overview);
		Send(reply.c_str());
	    }
	    Send(".");
	    continue;
	}

	ISIT("GROUP")					// RFC 977
	{
	    if ( arg1[0] == 0 )
	    {
	        Send("501 syntax error; expected 'GROUP <group-name>'");
		continue;
	    }

	    Group restore = group;

	    if ( group.Load(arg1) < 0 )
	    {
		sprintf(reply, "411 No such newsgroup: %s", 
		    (const char*)group.Errmsg());
		Send(reply);
		group = restore;
		continue;
	    }

	    // UPDATE CURRENT ARTICLE
	    article.Load(group.Name(), group.Start());

	    //   211 n f l s group selected
	    //           (n = estimated number of articles in group,
	    //           f = first article number in the group,
	    //           l = last article number in the group,
	    //           s = name of the group.)
	    //
	    sprintf(reply, "211 %lu %lu %lu %s group selected", 
		(unsigned long)group.Total(), 
		(unsigned long)group.Start(), 
		(unsigned long)group.End(), 
		(const char*)group.Name());
	    Send(reply);
	    continue;
	}

	ISIT("HELP")					// RFC 977
	{
	    Send("100 help text follows");
	    Send("CHECK\r\n"
	         "TAKETHIS\r\n"
	         "MODE [stream|reader]\r\n"
	         "LIST [active|active.times|distributions|distrib.pats|newsgroups|overview.fmt|subscriptions]\r\n"
	         "LISTGROUP [newsgroup]\r\n"
	         "XREPLIC\r\n"
	         "XOVER [msg#|msg#-|msg#-msg#]\r\n"
	         "GROUP newsgroup\r\n"
	         "HELP\r\n"
	         "NEWGROUPS [YY]yymmdd hhmmss [GMT|UTC] [distributions]\r\n"
	         "NEWNEWS\r\n"
	         "NEXT\r\n"
	         "HEAD [msg#|<msgid>]\r\n"
	         "BODY [msg#|<msgid>]\r\n"
	         "ARTICLE [msg#|<msgid>]\r\n"
	         "STAT [msg#|<msgid>]\r\n"
	         "POST\r\n"
	         "DATE\r\n"
	         "QUIT\r\n"
		 ".");
	    continue;
	}

	ISIT("NEWGROUPS")				// RFC 977
	{
	    // NEWGROUPS <YYMMDD> <HHMMSS> [GMT] [<distributions>]
	    if ( strlen(arg1) != 6 || strlen(arg2) != 6 )
	    {
	        Send("501 Bad or missing date/time arguments");
		continue;
	    }

	    int year, mon, day, hour, min, sec;
	    if ( sscanf(arg1, "%2d%2d%2d", &year, &mon, &day) != 3 ||
	         sscanf(arg2, "%2d%2d%2d", &hour, &min, &sec) != 3 )
	    {
	        Send("501 Bad date/time argument");
		continue;
	    }

	    // TBD
	    //     1) TRANSLATE YY -> YYYY
	    //     2) TRANSLATE TIMES INTO A time() VALUE
	    //     3) COMPARE TIME TO GROUP'S CTIME
	    //
	
	    vector<string> groupnames;
	    AllGroups(groupnames, NULL);

	    Send("231 list of new newsgroups follows");

	    for ( unsigned t=0; t<groupnames.size(); t++ )
	    {
		Group tgroup;
		if ( tgroup.Load(groupnames[t].c_str()) < 0 )
		    { continue; }
		Send(tgroup.Name());
	    }
	    Send(".");		// for now, nothing matches
	    continue;
	    
/**** TBD
		if ( tgroup.Ctime() > checktime )
		    { Send(tgroup.Name()); }
	    }
	    Send(".");
	    continue;
********/
	}

	ISIT("NEWNEWS")				// RFC 977
	{
	    Send("501 Command not implemented on server");	// TBD
	    continue;
	}

	ISIT("NEXT")				// RFC 977
	{
	    Article restore = article;

	    if ( ! group.IsValid() )
		{ Send("412 no newsgroup selected"); continue; }

	    if ( ! article.IsValid() )
		{ Send("420 no article has been selected"); continue; }

	    unsigned long next = article.Number() + 1;

	    if ( next < group.Start() || next > group.End() )
		{ Send("421 no next article in this group"); continue; }

	    if ( article.Load(group.Name(), next) < 0 )
	    {
	        sprintf(reply, "421 error retrieving article %lu: %s",
		    (unsigned long)next,
		    (const char*)article.Errmsg());
		Send(reply);
		article = restore;
		continue;
	    }

	    sprintf(reply, "223 %lu %s article retrieved - request text separately",
	        (unsigned long)next,
		(const char*)article.MessageID());

	    Send(reply);
	    continue;
	}

	if ( strcasecmp(cmd, "HEAD") == 0 ||	// RFC 977
	     strcasecmp(cmd, "BODY") == 0 ||	// RFC 977
	     strcasecmp(cmd, "ARTICLE") == 0 ||	// RFC 977
	     strcasecmp(cmd, "STAT") == 0 )	// RFC 977
	{
	    Article restore = article;

	    unsigned long the_article;
	    char restoreflag = 0;

	    if ( ! group.IsValid() )
		{ Send("412 Not currently in newsgroup"); continue; }

	    if ( arg1[0] == '<' )    // "ARTICLE <252-rush.general@news.3dsite.com>"
	    {
		if ( group.FindArticleByMessageID(arg1, the_article) < 0 )
		{
		    Send("430 no such article found");
		    continue;
                }
		restoreflag = 1;	// RFC 977: do not affect current article
	    }
	    else if ( isdigit(arg1[0]) )	// "HEAD 12"
	    {
	        if ( sscanf(arg1, "%lu", &the_article) != 1 )
		    { Send("501 bad article number"); continue; }
		restoreflag = 0;	// RFC 977: affect current article if valid
	    }
	    else if ( arg1[0] == 0 )	// "HEAD"
	    {
	        the_article = article.Number();
		restoreflag = 1;
	    }
	    else			// all else is junk
		{ Send("501 bad argument"); continue; }

	    // Range check
	    if ( the_article < group.Start() || the_article > group.End() )
	    {
		sprintf(reply, "423 no such article in group (range %lu-%lu)",
		    (unsigned long)group.Start(),
		    (unsigned long)group.End());
		Send(reply);
		continue;
	    }

	    if ( article.Load(group.Name(), the_article) < 0 )
	    {
		sprintf(reply, "430 no such article: %s", 
		    (const char*)article.Errmsg());
		Send(reply);
		continue;
	    }

	    // HANDLE VARIATIONS OF COMMAND
	    if ( strcasecmp(cmd, "ARTICLE") == 0 )
	    {
		sprintf(reply, 
		    "220 %lu %s article retrieved - head and body follow", 
		    (unsigned long)the_article, 
		    (const char*)article.MessageID());
		Send(reply);
	        article.SendArticle(msgsock); 
		Send(".");
	    }
	    else if ( strcasecmp(cmd, "HEAD") == 0 )
	    {
		sprintf(reply,
		    "221 %lu %s article retrieved - head follows", 
		    (unsigned long)the_article, 
		    (const char*)article.MessageID());
		Send(reply);
	        article.SendHead(msgsock);
		Send(".");
	    }
	    else if ( strcasecmp(cmd, "BODY") == 0 )
	    {
		sprintf(reply,
		    "222 %lu %s article retrieved - body follows", 
		    (unsigned long)the_article, 
		    (const char*)article.MessageID());
		Send(reply);
	        article.SendBody(msgsock);
		Send(".");
	    }
	    else if ( strcasecmp(cmd, "STAT") == 0 )
	    {
		sprintf(reply,
		    "223 %lu %s article retrieved - request text separately", 
		    (unsigned long)the_article, 
		    (const char*)article.MessageID());
		Send(reply);
	    }

	    if ( restoreflag )
	        { article = restore; }

	    continue;
	}

	ISIT("POST")					// RFC 977
	{
	    Send("340 Continue posting; Period on a line by itself to end");

	    // KEEP TRACK OF POSTING LENGTH
	    //    If too long, stop loading posting.
	    //
	    int linechars = 0,
	        linecount = 0,
	        toolong = 0;

	    // COLLECT POSTING FROM CLIENT
	    int eom = 0;
	    char c;
	    ssize_t rc;
	    string msg;

	    while ((rc = read(msgsock, &c, 1)) == 1 )
	    {
		// KEEP TRACK OF #LINES
		//    Lines longer than 80 chars count as multiple lines.
		//    If posting too long, stop accumulating message in ram,
		//    but keep reading until they've sent the terminating "."
		//
		++linechars;
		if ( linechars > 80 || c == '\n' )
		    { linechars = 0; linecount++; }
		if ( group.PostLimit() > 0 && linecount > group.PostLimit() )
		    { toolong = 1; continue; }

	        msg += c;

		// PARSE FOR END OF MESSAGE
		//    Cute little state machine to maintain end of message
		//    parse over buffer boundaries. The states:
		//
		//          0 - not yet at eom
		//          1 - \n before the dot
		//          2 - the dot
		//          3 - \r (or \n) after the dot
		//
		if (c == '\n' || c == '\r')
		{
		    if (eom == 0)
	        	eom = 1;
		    else if (eom == 2)
		    {
			eom = 3;
			break;
		    }
		}
		else if ( eom == 1 && ( c == '.' ) )
		    eom = 2;
		else if ( eom != 0 )
		    eom = 0;
	    }

	    if (rc < 0)
	    {
		G_conf.LogMessage(L_INFO, "Read error from %s (error = %s).",
		    GetRemoteIPStr(), strerror(errno));
		break;
	    }
	    else if (rc == 0)
	    {
		G_conf.LogMessage(L_INFO, "Read zero from %s.", GetRemoteIPStr());
		break;
	    }

	    // POSTING TOO LONG? FAIL
	    if ( toolong )
	    {
		sprintf(reply, "411 Not Posted: article exceeds sanity line limit of %d.", 
		    (int)group.PostLimit());
		Send(reply);
		continue;
	    }

	    if ( eom != 3 )
	    {
		snprintf(reply, sizeof reply,
		    "411 Not Posted: protocol error (missing end of message).");
		Send(reply);
		continue;
	    }

	    // PARSE ARTICLE -- SEPARATE HEADER AND BODY
	    vector<string> header;
	    vector<string> body;
	    if ( group.ParseArticle(msg, header, body) < 0 )
	    {
	        sprintf(reply, "441 %s", (const char*)group.Errmsg());
		Send(reply);
		continue;
	    }

	    // UPDATE 'Path:'
	    group.UpdatePath(header);

	    // POST ARTICLE
	    //    Don't affect 'current group' or 'current article'.
	    //
	    Group tgroup;
	    if ( tgroup.Post(overview, header, body, remoteip_str) < 0 )
	    {
	        sprintf(reply, "441 %s", (const char*)tgroup.Errmsg());
		Send(reply);
		continue;
	    }

	    Send("240 Article posted successfully.");

	    // CC MESSAGE TO MAIL ADDRESS?
	    if ( tgroup.IsCCPost() )
	    {
		string from = "Anonymous",
		       subject = "-";

		// PRESERVE THESE FIELDS FROM NNTP POSTING -> SMTP
		//    From:		-- must
		//    Subject:		-- must
		//    Xref:		-- ?
		//    Path:		-- ?
		//    References:	-- needed to preserve threading
		//    Message-ID:	-- needed to preserve threading
		//    Content-Type:	-- mime related
		//    MIME-Version:	-- mime related
		//
		int pflag = 0;
		string preserve;
		for ( unsigned t=0; t<header.size(); t++ )
		{
		    const char *head = header[t].c_str();

		    // CONTINUATION OF HEADER LINE?
		    if ( head[0] == ' ' || head[0] == 9 )
		    {
		        // CONTINUATION OF PREVIOUS PRESERVED HEADER LINE?
		        if ( pflag )
			    { preserve += header[t]; preserve += "\n"; }
			continue;
		    }

		    // ZERO OUT PRESERVE -- NO MORE CONTINUATIONS
		    pflag = 0;

		    // CHECK FOR PRESERVE FIELDS
		    if ( ISHEAD("From: ") ||
		         ISHEAD("Subject: ") ||
		         ISHEAD("References: ") ||
		         ISHEAD("Xref: ") ||
		         ISHEAD("Path: ") ||
		         ISHEAD("Content-Type: ") ||
		         ISHEAD("MIME-Version: ") ||
		         ISHEAD("Message-ID: ") )
		    {
		        pflag = 1;
			preserve += header[t];
			preserve += "\n";
		    }
		}

		FILE *fp = popen(G_conf.SendMail(), "w");
		fprintf(fp, "To: %s\n", (const char*)tgroup.VoidEmail());

		// Bcc list can be long; break it up into one line per address
		BreakLineToFP(fp, "Bcc: ", tgroup.CCPost(), "\n", ",");
		// fprintf(fp, "Bcc: %s\n", (const char*)tgroup.CCPost());
		fprintf(fp, "%s", preserve.c_str());

		// Reply-To: Needed for mail gateway
		if ( tgroup.IsReplyTo() )
		    fprintf(fp, "Reply-To: %s\n", (const char*)tgroup.ReplyTo());

		// Errors-To: advised so admin hears about problems, in addition
		//            to the real person who sent the message.
		//
		fprintf(fp, "Errors-To: %s", (const char*)tgroup.Creator());

		fprintf(fp, "\n");

		fprintf(fp, "[posted to %s]\n\n", (const char*)tgroup.Name());
		for ( unsigned t=0; t<body.size(); t++ )
		    fprintf(fp, "%s\n", (const char*)body[t].c_str());
		if ( pclose(fp) < 0 )
	            G_conf.LogMessage(L_ERROR, "ccpost pclose failed - %s",
		                      strerror(errno));
	    }
	    continue;
	}

	ISIT("DATE")					// COMMON EXTENSIONS - RFC 2980
	{
      	    // "111 YYYYMMDDhhmmss"
	    time_t lt = time(NULL);
	    struct tm *tm = gmtime(&lt);		// RFC 2980 -- time is GMT format, not local
	    sprintf(reply, "111 %d%02d%02d%02d%02d%02d",
	        (int)tm->tm_year + 1900,
	        (int)tm->tm_mon + 1,	// 0-11 -> 1-12
	        (int)tm->tm_mday,	// 1-31
	        (int)tm->tm_hour,	// 0-23
	        (int)tm->tm_min,	// 0-59
	        (int)tm->tm_sec);	// 0-59
	    Send(reply);
	    continue;
	}

	ISIT("QUIT")					// RFC 977
	{
	    Send("205 goodbye.");
	    break;
	}

	Send("500 Command not understood");
	continue; 
    }

    close(msgsock);
    G_conf.LogMessage(L_INFO, "Connection from %s closed", GetRemoteIPStr());

    return(0);
}

// OPEN A TCP LISTENER ON THE CONFIGURED ADDRESS AND PORT
int Server::Listen()
{
    if ((sock = socket (AF_INET,SOCK_STREAM,0)) < 0) 
	{ errmsg = "socket(): "; errmsg += strerror(errno); return(-1); }

    // Allow reuse of address to avoid "bind(): address already in use"
    {
        int on = 1;
	if ( setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(int)) < 0 )
	    { errmsg = "setsockopt(SO_REUSEADDR): "; errmsg += strerror(errno); return(-1); }
    }

    while (bind(sock, (struct sockaddr*)G_conf.Listen(),
                sizeof(struct sockaddr_in)) < 0) 
	{ perror("binding stream socket"); sleep(5); continue; }

    if ( listen(sock,5) < 0 )
	{ errmsg = "listen(sock,5): "; errmsg += strerror(errno); return(-1); }

    return(0);
}

// ACCEPT CONNECTIONS FROM REMOTE
int Server::Accept()
{
//    fprintf(stderr, "Listening for connect requests on port %d\n", 
//        (int)port);

#if defined(DARWIN) | defined(BSD)
    int length = sizeof(sin);
#else
    socklen_t length = sizeof(sin);
#endif

    msgsock = accept(sock, (struct sockaddr*)&sin, &length);
    if (msgsock < 0) 
    {
        errmsg = "accept(): ";
	errmsg += strerror(errno);
	return(-1);
    }

    G_conf.LogMessage(L_INFO, "Connection from host %s, port %u",
		      (const char*)inet_ntoa(sin.sin_addr),
		      (int)ntohs(sin.sin_port));

    return (0);
}
