# Nim editor
Nim editor is Nima's vi improved. Tries to clone brammool(RIP)'s [vim](https://github.com/vim/vim) as a C learning project. This project has been wrote from scratch with no help from vim's source code.

## Dependencies
This project is untested on windows. Though it should work with mingw64 as the project is tested with gcc. This project doesn't have any runtime dependencies. But compile dependencies are:
- git (for git cloning the project)
- gcc
- make

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

## Credits
My initial inspiration was from build your own x repository. Or to be exact, Kilo project. But I've changed the Kilo nano-like editor into a Vim clone. 
- [build your own x](https://github.com/codecrafters-io/build-your-own-x)
- [Kilo](https://viewsourcecode.org/snaptoken/kilo/)

