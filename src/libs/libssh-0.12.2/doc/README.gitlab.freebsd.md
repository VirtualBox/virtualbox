# Install a FreeBSD CI instance

Install the following packages:

```
pkg install -y bash git gmake cmake cmocka openssl wget pkgconf ccache bash
```

Create gitlab-runner user:

```
pw group add -n gitlab-runner
pw user add -n gitlab-runner -g gitlab-runner -s /usr/local/bin/bash
mkdir /home/gitlab-runner
chown gitlab-runner:gitlab-runner /home/gitlab-runner
```

Get the gitlab-runner binary for freebsd:

```
wget -O /usr/local/bin/gitlab-runner https://gitlab-runner-downloads.s3.amazonaws.com/latest/binaries/gitlab-runner-freebsd-amd64
chmod +x /usr/local/bin/gitlab-runner
```

Create a log file and allow access:

```
touch /var/log/gitlab_runner.log && chown gitlab-runner:gitlab-runner /var/log/gitlab_runner.log
```

We need a start script to run it on boot:

```
mkdir -p /usr/local/etc/rc.d
cat > /usr/local/etc/rc.d/gitlab_runner << EOF
#!/usr/local/bin/bash
# PROVIDE: gitlab_runner
# REQUIRE: DAEMON NETWORKING
# BEFORE:
# KEYWORD:

. /etc/rc.subr

name="gitlab_runner"
rcvar="gitlab_runner_enable"

load_rc_config $name

user="gitlab-runner"
user_home="/home/gitlab-runner"
command="/usr/local/bin/gitlab-runner run"
pidfile="/var/run/${name}.pid"

start_cmd="gitlab_runner_start"
stop_cmd="gitlab_runner_stop"
status_cmd="gitlab_runner_status"

gitlab_runner_start()
{
    export USER=${user}
    export HOME=${user_home}

    if checkyesno ${rcvar}; then
        cd ${user_home}
        /usr/sbin/daemon -u ${user} -p ${pidfile} ${command} > /var/log/gitlab_runner.log 2>&1
    fi
}

gitlab_runner_stop()
{
    if [ -f ${pidfile} ]; then
        kill `cat ${pidfile}`
    fi
}

gitlab_runner_status()
{
    if [ ! -f ${pidfile} ] || kill -0 `cat ${pidfile}`; then
        echo "Service ${name} is not running."
    else
        echo "${name} appears to be running."
    fi
}

run_rc_command $1
EOF
chmod +x /usr/local/etc/rc.d/gitlab_runner
```

Register your gitlab-runner with your gitlab project

```
su gitlab-runner -c 'gitlab-runner register'
```

Start the gitlab runner service:

```
sysrc -f /etc/rc.conf "gitlab_runner_enable=YES"
service gitlab_runner start
```
