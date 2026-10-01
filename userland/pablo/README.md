# Pablo

Pablo is the PID 1 service supervisor for Narc. It loads global settings from
`/etc/pablo.conf` and one service per file from `/etc/pablo/services`.

## Service files

Service filenames end in `.service`; the filename without the suffix is the
service name. Entries use `key=value` syntax.

```ini
exec=/bin/example
policy=respawn
after=storage
arg=--foreground
```

`exec` is required and must be an absolute path. `policy` accepts `once` or
`respawn`; the default is `once`. `after` names one dependency. Each `arg`
appends one argument in declaration order.

Pablo rejects duplicate names, missing dependencies and dependency cycles.
Services are loaded in filename order. Failed dependencies propagate failure,
and respawning services use the global restart limit and delay.

## Global settings

```ini
restart_limit=5
restart_delay=64
```

The delay is measured in supervisor ticks. Unknown or malformed settings stop
boot instead of being ignored.
