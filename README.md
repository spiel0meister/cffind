# cffind

Fuzzy search C function definitions.

## Build

```console
$ gcc -o cbuild cbuild.c
$ ./cbuild
```

## Quick Start

```console
$ ./cffind "<QUERY>" <FILES...>
```

Queries have the following syntax:
    \<RETURN\>(\<PARAMS\>)

You can also use "*" as a wildcard.

## Disclaimer

- Pointers **NOT SUPPORTED**
