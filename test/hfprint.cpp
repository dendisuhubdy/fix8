//-----------------------------------------------------------------------------------------
#if 0

Fix8 is released under the New BSD License.

Copyright (c) 2010-12, David L. Dight <fix@fix8.org>
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are
permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this list of
	 	conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice, this list
	 	of conditions and the following disclaimer in the documentation and/or other
		materials provided with the distribution.
    * Neither the name of the author nor the names of its contributors may be used to
	 	endorse or promote products derived from this software without specific prior
		written permission.
    * Products derived from this software may not be called "Fix8", nor can "Fix8" appear
	   in their name without written permission from fix8.org

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS
OR  IMPLIED  WARRANTIES,  INCLUDING,  BUT  NOT  LIMITED  TO ,  THE  IMPLIED  WARRANTIES  OF
MERCHANTABILITY AND  FITNESS FOR A PARTICULAR  PURPOSE ARE  DISCLAIMED. IN  NO EVENT  SHALL
THE  COPYRIGHT  OWNER OR  CONTRIBUTORS BE  LIABLE  FOR  ANY DIRECT,  INDIRECT,  INCIDENTAL,
SPECIAL,  EXEMPLARY, OR CONSEQUENTIAL  DAMAGES (INCLUDING,  BUT NOT LIMITED TO, PROCUREMENT
OF SUBSTITUTE  GOODS OR SERVICES; LOSS OF USE, DATA,  OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED  AND ON ANY THEORY OF LIABILITY, WHETHER  IN CONTRACT, STRICT  LIABILITY, OR
TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE
EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#endif
//-----------------------------------------------------------------------------------------
/** \file hfprint.cpp
\n
  This is a simple logfile/logstream printer using the metadata generated for hftest.cpp.\n
\n
<tt>
	hfprint -- f8 protocol log printer\n
\n
	Usage: hfprint [-hosv] <fix protocol file, use '-' for stdin>\n
		-h,--help               help, this screen\n
		-o,--offset             bytes to skip on each line before parsing FIX message\n
		-s,--summary            summary, generate message summary\n
		-v,--version            print version then exit\n
	e.g.\n
		hfprint myfix_server_protocol.log\n
		cat myfix_client_protocol.log | hfprint -\n
</tt>
*/
//-----------------------------------------------------------------------------------------
#include <iostream>
#include <memory>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>
#include <map>
#include <list>
#include <set>
#include <iterator>
#include <algorithm>
#include <bitset>
#include <typeinfo>
#include <sys/ioctl.h>
#include <signal.h>
#include <termios.h>

#include <regex.h>
#include <errno.h>
#include <string.h>

// f8 headers
#include <f8includes.hpp>

#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif

#include <usage.hpp>
#include <consolemenu.hpp>
#include "Perf_types.hpp"
#include "Perf_router.hpp"
#include "Perf_classes.hpp"
#include "hftest.hpp"

//-----------------------------------------------------------------------------------------
using namespace std;
using namespace FIX8;

//-----------------------------------------------------------------------------------------
void print_usage();
const string GETARGLIST("hsvo:c");
bool term_received(false), summary(false);

typedef map<string, unsigned> MessageCount;

//-----------------------------------------------------------------------------------------
void sig_handler(int sig)
{
   switch (sig)
   {
   case SIGTERM:
   case SIGINT:
      term_received = true;
      signal(sig, sig_handler);
      break;
   }
}

//----------------------------------------------------------------------------------------
/// Abstract file or stdin input.
class filestdin
{
   std::istream *ifs_;
   bool nodel_;

public:
   filestdin(std::istream *ifs, bool nodel=false) : ifs_(ifs), nodel_(nodel) {}
   ~filestdin() { if (!nodel_) delete ifs_; }

   std::istream& operator()() { return *ifs_; }
};

//-----------------------------------------------------------------------------------------
#if defined PERMIT_CUSTOM_FIELDS
#include "myfix_custom.hpp"
#endif

//-----------------------------------------------------------------------------------------
int main(int argc, char **argv)
{
	int val, offset(0);

#ifdef HAVE_GETOPT_LONG
	option long_options[] =
	{
		{ "help",			0,	0,	'h' },
		{ "offset",			1,	0,	'o' },
		{ "version",		0,	0,	'v' },
		{ "summary",		0,	0,	's' },
		{ "context",		0,	0,	'c' },
		{ 0 },
	};

	while ((val = getopt_long (argc, argv, GETARGLIST.c_str(), long_options, 0)) != -1)
#else
	while ((val = getopt (argc, argv, GETARGLIST.c_str())) != -1)
#endif
	{
      switch (val)
		{
		case 'v':
			cout << argv[0] << " for "PACKAGE" version "VERSION << endl;
			return 0;
		case ':': case '?': return 1;
		case 'h': print_usage(); return 0;
		case 'o': offset = GetValue<int>(optarg); break;
		case 's': summary = true; break;
		case 'c':
			 cout << "Context FIX beginstring:" << TEX::ctx._beginStr << endl;
			 cout << "Context FIX version:" << TEX::ctx.version() << endl;
			 return 0;
		default: break;
		}
	}

	signal(SIGTERM, sig_handler);
	signal(SIGINT, sig_handler);

	string inputFile;
	if (optind < argc)
		inputFile = argv[optind];
	if (inputFile.empty())
	{
		print_usage();
		return 1;
	}

	bool usestdin(inputFile == "-");
   filestdin ifs(usestdin ? &cin : new ifstream(inputFile.c_str()), usestdin);
	if (!ifs())
	{
		cerr << "Could not open " << inputFile << endl;
		return 1;
	}

	unsigned msgs(0);
	MessageCount *mc(summary ? new MessageCount : 0);

#if defined PERMIT_CUSTOM_FIELDS
	TEX::myfix_custom custfields(true); // will cleanup; modifies ctx
	TEX::ctx.set_ube(&custfields);
#endif

	const int bufsz(4096);
	char buffer[bufsz];

	try
	{
		while (!ifs().eof() && !term_received)
		{
			ifs().getline(buffer, bufsz);
			if (buffer[0])
			{
				scoped_ptr<Message> msg(Message::factory(TEX::ctx, buffer + offset));
				if (summary)
				{
					MessageCount::iterator mitr(mc->find(msg->get_msgtype()));
					if (mitr == mc->end())
						mc->insert(MessageCount::value_type(msg->get_msgtype(), 1));
					else
						mitr->second++;
				}
				cout << *msg << endl;
				++msgs;
			}
		}

		if (term_received)
			cerr << "interrupted" << endl;
	}
	catch (f8Exception& e)
	{
		cerr << "exception: " << e.what() << endl;
	}

	cout << msgs << " messages decoded." << endl;
	if (summary)
	{
		for (MessageCount::const_iterator mitr(mc->begin()); mitr != mc->end(); ++mitr)
		{
			const BaseMsgEntry *bme(TEX::ctx._bme.find_ptr(mitr->first));
			cout << setw(20) << left << bme->_name << " (\"" << mitr->first << "\")" << '\t' << mitr->second << endl;
		}
	}

	return 0;
}

//-----------------------------------------------------------------------------------------
void print_usage()
{
	UsageMan um("hfprint", GETARGLIST, "<fix protocol file, use '-' for stdin>");
	um.setdesc("hfprint -- f8 protocol log printer");
	um.add('h', "help", "help, this screen");
	um.add('v', "version", "print version then exit");
	um.add('o', "offset", "bytes to skip on each line before parsing FIX message");
	um.add('s', "summary", "summary, generate message summary");
	um.add("e.g.");
	um.add("@hfprint myfix_server_protocol.log");
	um.add("@hfprint hfprint -s -o 12 myfix_client_protocol.log");
	um.add("@cat myfix_client_protocol.log | hfprint -");
	um.print(cerr);
}
