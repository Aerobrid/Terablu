# Terablu
This repository is my implementation of [Clox](https://github.com/munificent/craftinginterpreters/tree/master/c) from the book [Crafting Interpreters](https://craftinginterpreters.com/) by Robert Nystrom.  <br/> 
CLox is a C-Based interpreter for the toy language Lox, it contains no external libraries. <br/>
# How to run
For compiling the interpreter on Windows, since main Clox repo already has tutorial for counterpart on Linux/Unix System:
<br/>

```
gcc -o clox chunk.c compiler.c debug.c main.c memory.c object.c scanner.c table.c value.c vm.c -std=c99
```

And then executing the interpreter is as easy as this:

``` 
./clox
 ```

## What's new
To be added
