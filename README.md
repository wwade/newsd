newsd
=====

newsd -- Standalone Local NNTP News Server

WHAT IS IT
----------
    
Newsd is a standalone local NNTP news server for private newsgroup serving
on a single server.  It is useful for serving private newsgroup(s)
to an intranet or the Internet and can act as a simple mail gateway,
however it does not interface with other news servers and cannot manage
distributed news feeds, i.e. Usenet news.
    

LICENSING
---------

Newsd comes with complete free source code.  Newsd is available under
the terms of the GNU General Public License.  See the file "LICENSE"
for more info.


BUILD INSTRUCTIONS
------------------

Run the following commands to build the newsd software:

    ./configure --prefix=/some/directory
    make

Once the software is compiled, use the following command to install it:

    make install


DOCUMENTATION
-------------

See the "newsd" and "newsd.conf" man pages for more information.


FEATURES
--------

Newsd provides simple file-based system administration.  No satellite
binaries or scripts are needed to install, configure, administer, or
maintain newsd.


LIMITATIONS
-----------
   
Refer to the "newsd" man page.


REPORTING BUGS
--------------

Please report bugs via github:

   [https://github.com/wwade/newsd](https://github.com/wwade/newsd)
