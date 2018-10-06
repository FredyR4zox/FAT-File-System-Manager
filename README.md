# FAT-File-System-Manager
Second project for the subject Operating Systems, which consisted in developing a file system manager for the FAT (File Allocation Table) architecture.


### Compilation
``` bash
$ gcc vfs.c -Wall -lreadline -o vfs
```


### Usage
The following commands where implemented:
##### Directory manipulation functions
| Command | Explanation |
| ------- | ----------- |
| ls | list the contents of the current directory |
| mkdir dir | creates a subdirectory named dir in the current directory |
| cd dir | move current directory to dir |
| pwd | writes the absolute path of the current directory |
| rmdir dir | removes the dir subdirectory (if empty) from the current directory |
##### File manipulation functions
| Command | Explanation |
| ------- | ----------- |
| get file1 file2 | copies a standard UNIX file file1 to a file in our system file2 |
| put file1 file2 | copy a file from our system file1 to a normal UNIX file file2 |
| cat file | writes the contents of the file file to the screen |
| cp file1 file2 | copy the file file1 to file2 |
| cp file1 dir   | copy the file file to the dir subdirectory |
| mv file1 file2 | move file from file1 to file2 |
| mv file1 dir   | move the file file to the directory dir |
| rm file | removes the file file |

#### Example:
``` bash
$ ./vfs Cdisk
vfs$ mkdir hello
vfs$ cd hello
vfs$ get vfs.c my_program.c
vfs$ cat my_program.c
```


### Files
* vfs.c - Functions listed above (Tables "Directory manipulation functions" and "File manipulation functions") and get_free_block, free_block, find_dir_entry, cstr_cmp and getMonthName made by me.


### Authors
* Frederico Emanuel Almeida Lopes - up201604674 - [FredyR4zox](https://www.github.com/FredyR4zox)
