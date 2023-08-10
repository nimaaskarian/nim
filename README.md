# Nim editor
Nim editor is Nima's vi improved. Tries to clone brammool(RIP)'s [vim](https://github.com/vim/vim) as a C learning project. This project has been wrote from scratch with no help from vim's source code.

## Installation
Installation path is `$(DESTDIR)$(PREFIX)/bin`. You can set these two variable like the example below to install inside `~/.local/bin`.  
```
export DESTDIR=~/.local
```
Clone this git repository and change directory into it:  
```
git clone https://github.com/nimaaskarian/nim
cd nim
```
Then you can:
- Compile (makes nim inside git repository's folder. Can be executed using `./nim`)
```
make
```
- Install (if your installation path is owned by root, you need root privileges):
```
make install
```
## Uninstall
For uninstallation, set `$DESTDIR` and `$PREFIX` variables like you've set them when you were installing. Then:  
```
make uninstall
```
You can also use `make clean` to remove compiled binary from repository's folder.
