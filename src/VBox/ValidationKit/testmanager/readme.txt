$Id: readme.txt 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

Directory descriptions:
    ./                  The Test Manager.
    ./batch/            Batch scripts to be run via cron.
    ./cgi/              CGI scripts (we'll use standard CGI at first).
    ./core/             The core Test Manager logic (model).
    ./htdocs/           Files to be served directly by the web server.
    ./htdocs/css/       Style sheets.
    ./htdocs/images/    Graphics.
    ./webui/            The Web User Interface (WUI) bits. (Not sure if we will
                        do model-view-controller stuff, though. Time will show.)

I.  Running a Test Manager instance with Docker:

  - This way should be preferred to get a local Test Manager instance running
    and is NOT meant for production use!

  - Install docker-ce and docker-compose on your Linux host (not tested on
    Windows yet). Your user must be able to run the Docker CLI (see Docker documentation).

  - Type "kmk" to get the containers built, "kmk start|stop" to start/stop them
    respectively. To start over, use "kmk clean". For having a peek into the container
    logs, use "kmk logs".

    To administrate / develop the database, an Adminer instance is running at
    http://localhost:8080

    To access the actual Test Manager instance, go to http://localhost:8080/testmanager/

  - There are two ways of doing development with this setup:

    a. The Test Manager source is stored inside a separate data volume called
       "docker_vbox-testmgr-web". The source will be checked out automatically on
       container initialization.  Development then can take part within that data
       container. The initialization script will automatically pull the sources
       from the public OSE tree, so make sure this is what you want!

    b. Edit the (hidden) .env file in this directory and change VBOX_TESTMGR_DATA
       to point to your checked out VBox root, e.g. VBOX_TESTMGR_DATA=/path/to/VBox/trunk


II. Steps for manually setting up a local Test Manager instance for development:

  - Install apache, postgresql, python, psycopg2 (python) and pylint.

  - Create the database by executing 'kmk load-testmanager-db' in
    the './db/' subdirectory.   The default psql parameters there
    requies pg_hba.conf to specify 'trust' instead of 'peer' as the
    authentication method for local connections.

  - Use ./db/partial-db-dump.py on the production system to extract a
    partial database dump (last 14 days).

  - Use ./db/partial-db-dump.py with the --load-dump-into-database
    parameter on the development box to load the dump.

  - Configure apache using the ./apache-template-2.4.conf (see top of
    file for details), for example:

        Define TestManagerRootDir "/mnt/scratch/vbox/svn/trunk/src/VBox/ValidationKit/testmanager"
        Define VBoxBuildOutputDir "/tmp"
        Include "${TestManagerRootDir}/apache-template-2.4.conf"

    Make sure to enable cgi (a2enmod cgi && systemctl restart apache2).

  - Default htpasswd file has users a user 'admin' with password 'admin' and a
    'test' user with password 'test'.  This isn't going to get you far if
    you've  loaded something from the production server as there is typically
    no 'admin' user in the 'Users' table there.  So, you will need to add your
    user and a throwaway password to 'misc/htpasswd-sample' using the htpasswd
    utility.

  - Try http://localhost/testmanager/ in a browser and see if it works.


III. OS X version of the above manual setup using MacPorts:

  - sudo ports install apache2 postgresql12 postgresql12-server py38-psycopg2 py38-pylint
    sudo port select --set python python38
    sudo port select --set python3 python38
    sudo port select --set pylint pylint38

    Note! Replace the python 38 with the most recent one you want to use.  Same
          for the 12 in relation to postgresql.

  - Do what the postgresql12-server notes says, at the time of writing:
    sudo mkdir -p /opt/local/var/db/postgresql12/defaultdb
    sudo chown postgres:postgres /opt/local/var/db/postgresql12/defaultdb
    sudo su postgres -c 'cd /opt/local/var/db/postgresql12 && /opt/local/lib/postgresql12/bin/initdb -D /opt/local/var/db/postgresql12/defaultdb'
    sudo port load postgresql12-server

    Note! The postgresql12-server's config is 'trust' already, so no need to
          edit /opt/local/var/db/postgresql12/defaultdb/pg_hba.conf there.  If
          you use a different version, please check it.

  - kmk load-testmanager-db

  - Creating and loading a partial database dump as detailed above.

  - Configure apache:
      - sudo joe /opt/local/etc/apache2/httpd.conf:
        - Uncomment the line "LoadModule cgi_module...".
        - At the end of the file add (edit paths):
            Define TestManagerRootDir "/Users/bird/coding/vbox/svn/trunk/src/VBox/ValidationKit/testmanager"
            Define VBoxBuildOutputDir "/tmp"
            Include "${TestManagerRootDir}/apache-template-2.4.conf"
      - Test the config:
          /opt/local/sbin/apachectl -t
      - So apache will find the right python add the following to
        /opt/local/sbin/envvars:
          PATH=/opt/local/bin:/opt/local/sbin:$PATH
          export PATH
      - Load the apache service (or reload it):
          sudo port load apache2
      - Give apache access to read everything under TestManagerRootDir:
          chmod -R a:rX /Users/bird/coding/vbox/svn/trunk/src/VBox/ValidationKit/testmanager
          MYDIR=/Users/bird/coding/vbox/svn/trunk/src/VBox/ValidationKit; while [ '!' "$MYDIR" '<' "$HOME" ]; do \
              chmod a+x "$MYDIR"; MYDIR=`dirname $MYDIR`; done

  - Fix htpasswd file as detailed above and try the url (also above).


N.B. For developing tests (../tests/), setting up a local test manager will be
     a complete waste of time.  Just run the test drivers locally.
