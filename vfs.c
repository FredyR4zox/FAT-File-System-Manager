///////////////////////////////////////////////////////////////////
//                                                               //
//                Project II: File System Manager                //
//                                                               //
// Compilation: gcc vfs.c -Wall -lreadline -o vfs                //
// Usage: ./vfs [-b[128|256|512|1024]] [-f[7|8|9|10]] FILESYSTEM //
//                                                               //
///////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <readline/readline.h>
#include <readline/history.h>

#define MAXARGS 100
#define CHECK_NUMBER 9999
#define TYPE_DIR 'D'
#define TYPE_FILE 'F'
#define MAX_NAME_LENGHT 20

#define FAT_ENTRIES(TYPE) ((TYPE) == 7 ? 128 : (TYPE) == 8 ? 256 : (TYPE) == 9 ? 512 : 1024)
#define FAT_SIZE(TYPE) (FAT_ENTRIES(TYPE) * sizeof(int))
#define BLOCK(N) (blocks + (N) * sb->block_size)
#define DIR_ENTRIES_PER_BLOCK (sb->block_size / sizeof(dir_entry))

typedef struct command {
  char *cmd;              // string with just the main command
  int argc;               // number of arguments
  char *argv[MAXARGS+1];  // vector containing the arguments
} COMMAND;

typedef struct superblock_entry {
  int check_number;   // number that allows to identify the system as valid
  int block_size;     // block size {128, 256 (default), 512 or 1024 bytes}
  int fat_type;       // FAT type {7, 8 (default), 9 or 10}
  int root_block;     // number of the first block for the root directory
  int free_block;     // number of the first block in the list of free blocks
  int n_free_blocks;  // total number of free blocks
} superblock;

typedef struct directory_entry {
  char type;                   // entry type (TYPE_DIR or TYPE_FILE)
  char name[MAX_NAME_LENGHT];  // entry name
  unsigned char day;           // day where it was created (between 1 and 31)
  unsigned char month;         // month where it was created (between 1 and 12)
  unsigned char year;          // year where it was created (between 0 and 255 - 0 representa o ano de 1900)
  int size;                    // size in bytes (0 if TYPE_DIR)
  int first_block;             // first data block
} dir_entry;

// global variables
superblock *sb;   // superblock of the file system
int *fat;         // pointer to the FAT table
char *blocks;     // pointer to data region
int current_dir;  // block of current directory

// auxiliary functions
COMMAND parse(char *);
void parse_argv(int, char **);
void show_usage_and_exit(void);
void init_filesystem(int, int, char *);
void init_superblock(int, int);
void init_fat(void);
void init_dir_block(int, int);
void init_dir_entry(dir_entry *, char, char *, int, int);
void exec_com(COMMAND);

// directory manipulation functions
void vfs_ls(void);
void vfs_mkdir(char *);
void vfs_cd(char *);
void vfs_pwd(void);
void vfs_rmdir(char *);

// file manipulation functions
void vfs_get(char *, char *);
void vfs_put(char *, char *);
void vfs_cat(char *);
void vfs_cp(char *, char *);
void vfs_mv(char *, char *);
void vfs_rm(char *);


int main(int argc, char *argv[]) {
  char *linha;
  COMMAND com;

  parse_argv(argc, argv);
  while (1) {
    if ((linha = readline("vfs$ ")) == NULL) {
      free(linha);
      exit(0);
    }
    if (strlen(linha) != 0) {
      add_history(linha);
      com = parse(linha);
      exec_com(com);
    }
    free(linha);
  }
  return 0;
}


COMMAND parse(char *linha) {
  int i = 0;
  COMMAND com;

  com.cmd = strtok(linha, " ");
  com.argv[0] = com.cmd;
  while ((com.argv[++i] = strtok(NULL, " ")) != NULL);
  com.argc = i;
  return com;
}


void parse_argv(int argc, char *argv[]) {
  int i, block_size, fat_type;

  // default values
  block_size = 256;
  fat_type = 8;
  if (argc < 2 || argc > 4) {
    printf("vfs: invalid number of arguments\n");
    show_usage_and_exit();
  }
  for (i = 1; i < argc - 1; i++) {
    if (argv[i][0] == '-') {
      if (argv[i][1] == 'b') {
	block_size = atoi(&argv[i][2]);
	if (block_size != 128 && block_size != 256 && block_size != 512 && block_size != 1024) {
	  printf("vfs: invalid block size (%d)\n", block_size);
	  show_usage_and_exit();
	}
      } else if (argv[i][1] == 'f') {
	fat_type = atoi(&argv[i][2]);
	if (fat_type != 7 && fat_type != 8 && fat_type != 9 && fat_type != 10) {
	  printf("vfs: invalid fat type (%d)\n", fat_type);
	  show_usage_and_exit();
	}
      } else {
	printf("vfs: invalid argument (%s)\n", argv[i]);
	show_usage_and_exit();
      }
    } else {
      printf("vfs: invalid argument (%s)\n", argv[i]);
      show_usage_and_exit();
    }
  }
  init_filesystem(block_size, fat_type, argv[argc-1]);
  return;
}


void show_usage_and_exit(void) {
  printf("Usage: vfs [-b[128|256|512|1024]] [-f[7|8|9|10]] FILESYSTEM\n");
  exit(1);
}


void init_filesystem(int block_size, int fat_type, char *filesystem_name) {
  int fsd, filesystem_size;

  if ((fsd = open(filesystem_name, O_RDWR)) == -1) {
    // the file system doesnt exist --> it needs to be created and formatted
    if ((fsd = open(filesystem_name, O_CREAT | O_TRUNC | O_RDWR, S_IRWXU)) == -1) {
      printf("vfs: cannot create filesystem (%s)\n", filesystem_name);
      show_usage_and_exit();
    }

    // calculates the size of the file system
    filesystem_size = block_size + FAT_SIZE(fat_type) + FAT_ENTRIES(fat_type) * block_size;
    printf("vfs: formatting virtual file-system (%d bytes) ... please wait\n", filesystem_size);

    // extends the file system to the desired size
    lseek(fsd, filesystem_size - 1, SEEK_SET);
    write(fsd, "", 1);

    // maps the file system and starts the global variables
    if ((sb = (superblock *) mmap(NULL, filesystem_size, PROT_READ | PROT_WRITE, MAP_SHARED, fsd, 0)) == MAP_FAILED) {
      close(fsd);
      printf("vfs: cannot map filesystem (mmap error)\n");
      exit(1);
    }
    fat = (int *) ((unsigned long int) sb + block_size);
    blocks = (char *) ((unsigned long int) fat + FAT_SIZE(fat_type));
    
    // initiates the superblock
    init_superblock(block_size, fat_type);
    
    // initiates FAT
    init_fat();
    
    // starts the root directory block '/'
    init_dir_block(sb->root_block, sb->root_block);
  } else {
    // calculates the size of the file system
    struct stat buf;
    stat(filesystem_name, &buf);
    filesystem_size = buf.st_size;

    // maps the file system and starts the global variables
    if ((sb = (superblock *) mmap(NULL, filesystem_size, PROT_READ | PROT_WRITE, MAP_SHARED, fsd, 0)) == MAP_FAILED) {
      close(fsd);
      printf("vfs: cannot map filesystem (mmap error)\n");
      exit(1);
    }
    fat = (int *) ((unsigned long int) sb + sb->block_size);
    blocks = (char *) ((unsigned long int) fat + FAT_SIZE(sb->fat_type));

    // test if the file system is valid
    if (sb->check_number != CHECK_NUMBER || filesystem_size != sb->block_size + FAT_SIZE(sb->fat_type) + FAT_ENTRIES(sb->fat_type) * sb->block_size) {
      munmap(sb, filesystem_size);
      close(fsd);
      printf("vfs: invalid filesystem (%s)\n", filesystem_name);
      show_usage_and_exit();
    }
  }
  close(fsd);

  // starts the current directory
  current_dir = sb->root_block;
  return;
}


void init_superblock(int block_size, int fat_type) {
  sb->check_number = CHECK_NUMBER;
  sb->block_size = block_size;
  sb->fat_type = fat_type;
  sb->root_block = 0;
  sb->free_block = 1;
  sb->n_free_blocks = FAT_ENTRIES(fat_type) - 1;
  return;
}


void init_fat(void) {
  int i;

  fat[0] = -1;
  for (i = 1; i < sb->n_free_blocks; i++)
    fat[i] = i + 1;
  fat[sb->n_free_blocks] = -1;
  return;
}


void init_dir_block(int block, int parent_block) {
  dir_entry *dir = (dir_entry *) BLOCK(block);
  // the number of entries in the directory (initially 2) is saved in the size field of the entry "."
  init_dir_entry(&dir[0], TYPE_DIR, ".", 2, block);
  init_dir_entry(&dir[1], TYPE_DIR, "..", 0, parent_block);
  return;
}


void init_dir_entry(dir_entry *dir, char type, char *name, int size, int first_block) {
  time_t cur_time = time(NULL);
  struct tm *cur_tm = localtime(&cur_time);

  dir->type = type;
  strcpy(dir->name, name);
  dir->day = cur_tm->tm_mday;
  dir->month = cur_tm->tm_mon + 1;
  dir->year = cur_tm->tm_year;
  dir->size = size;
  dir->first_block = first_block;
  return;
}


void exec_com(COMMAND com) {
  // for each command invoke the function that implements it
  if (!strcmp(com.cmd, "exit")) {
    exit(0);
  } else if (!strcmp(com.cmd, "ls")) {
    if (com.argc > 1)
      printf("ERROR(input: 'ls' - too many arguments)\n");
    else
      vfs_ls();
  } else if (!strcmp(com.cmd, "mkdir")) {
    if (com.argc < 2)
      printf("ERROR(input: 'mkdir' - too few arguments)\n");
    else if (com.argc > 2)
      printf("ERROR(input: 'mkdir' - too many arguments)\n");
    else
      vfs_mkdir(com.argv[1]);
  } else if (!strcmp(com.cmd, "cd")) {
    if (com.argc < 2)
      printf("ERROR(input: 'cd' - too few arguments)\n");
    else if (com.argc > 2)
      printf("ERROR(input: 'cd' - too many arguments)\n");
    else
      vfs_cd(com.argv[1]);
  } else if (!strcmp(com.cmd, "pwd")) {
    if (com.argc != 1)
      printf("ERROR(input: 'pwd' - too many arguments)\n");
    else
      vfs_pwd();
  } else if (!strcmp(com.cmd, "rmdir")) {
    if (com.argc < 2)
      printf("ERROR(input: 'rmdir' - too few arguments)\n");
    else if (com.argc > 2)
      printf("ERROR(input: 'rmdir' - too many arguments)\n");
    else
      vfs_rmdir(com.argv[1]);
  } else if (!strcmp(com.cmd, "get")) {
    if (com.argc < 3)
      printf("ERROR(input: 'get' - too few arguments)\n");
    else if (com.argc > 3)
      printf("ERROR(input: 'get' - too many arguments)\n");
    else
      vfs_get(com.argv[1], com.argv[2]);
  } else if (!strcmp(com.cmd, "put")) {
    if (com.argc < 3)
      printf("ERROR(input: 'put' - too few arguments)\n");
    else if (com.argc > 3)
      printf("ERROR(input: 'put' - too many arguments)\n");
    else
      vfs_put(com.argv[1], com.argv[2]);
  } else if (!strcmp(com.cmd, "cat")) {
    if (com.argc < 2)
      printf("ERROR(input: 'cat' - too few arguments)\n");
    else if (com.argc > 2)
      printf("ERROR(input: 'cat' - too many arguments)\n");
    else
      vfs_cat(com.argv[1]);
  } else if (!strcmp(com.cmd, "cp")) {
    if (com.argc < 3)
      printf("ERROR(input: 'cp' - too few arguments)\n");
    else if (com.argc > 3)
      printf("ERROR(input: 'cp' - too many arguments)\n");
    else
      vfs_cp(com.argv[1], com.argv[2]);
  } else if (!strcmp(com.cmd, "mv")) {
    if (com.argc < 3)
      printf("ERROR(input: 'mv' - too few arguments)\n");
    else if (com.argc > 3)
      printf("ERROR(input: 'mv' - too many arguments)\n");
    else
      vfs_mv(com.argv[1], com.argv[2]);
  } else if (!strcmp(com.cmd, "rm")) {
    if (com.argc < 2)
      printf("ERROR(input: 'rm' - too few arguments)\n");
    else if (com.argc > 2)
      printf("ERROR(input: 'rm' - too many arguments)\n");
    else
      vfs_rm(com.argv[1]);
  } else
    printf("ERROR(input: command not found)\n");
  return;
}

int get_free_block(){
  if(sb->n_free_blocks == 0)
    return -1;

  int free_block = sb->free_block;
  sb->free_block = fat[free_block];
  fat[free_block] = -1;

  sb->n_free_blocks--;

  return free_block;
}

void free_block(int block){
  fat[block] = sb->free_block;
  sb->free_block = block;

  sb->n_free_blocks++;

  return;
}

//find a directory entry in the directory
dir_entry* find_dir_entry(dir_entry *base_dir_entry, char* name){
    int aux_dir = base_dir_entry->first_block;

    unsigned int aux_counter = 0;
    unsigned int n_of_dir_entries = DIR_ENTRIES_PER_BLOCK;

    while(aux_dir != -1){
        dir_entry *aux_dir_entry = (dir_entry *)BLOCK(aux_dir);
        
        for(short unsigned int i=0; i<n_of_dir_entries && aux_counter<base_dir_entry->size; i++, aux_counter++){
            if(!strcmp(aux_dir_entry->name, name))
                return aux_dir_entry;

            aux_dir_entry++;
        }

        aux_dir = fat[aux_dir];
    }

    return NULL;
}

int cstr_cmp(const void *a, const void *b) {
  const char **ia = (const char **)a;
  const char **ib = (const char **)b;
  return strcmp(*ia, *ib);
}

const char* getMonthName(unsigned int month){
   switch (month){
      case 1: return "Jan";
      case 2: return "Feb";
      case 3: return "Mar";
      case 4: return "Apr";
      case 5: return "May";
      case 6: return "Jun";
      case 7: return "Jul";
      case 8: return "Aug";
      case 9: return "Sep";
      case 10: return "Oct";
      case 11: return "Nov";
      case 12: return "Dec";
      default: return "Undefined";
   }

   return "Undefined";
}

// ls - list the contents of the current directory
void vfs_ls(void) {
  dir_entry *dir = (dir_entry *)BLOCK(current_dir);
  int n_entries = dir[0].size;

  char type_str[128];
  char **content = (char **)malloc(n_entries * sizeof(char *));
  for(int i = 0; i < n_entries; i++)
    content[i] = (char * )malloc(1024 * sizeof(char *));
  

  int cur_block = current_dir;
  for(int i = 0; i < n_entries; i++) {
    if(i % DIR_ENTRIES_PER_BLOCK == 0 && i) {
      cur_block = fat[cur_block];
      dir = (dir_entry *)BLOCK(cur_block);
    }

    int block_i = i % DIR_ENTRIES_PER_BLOCK;

    if(dir[block_i].type == TYPE_DIR)
      sprintf(type_str, "DIR");
    else if(dir[block_i].type == TYPE_FILE)
      sprintf(type_str, "%d", dir[block_i].size);
    else {
      printf("ERROR(filesystem: file type not recognized)\n");
      for(int j = 0; j < n_entries; j++)
        free(content[j]);
      free(content);
      return;
    }
    
    sprintf(content[i], "%*s\t%02d-%s-%04d\t%s", -MAX_NAME_LENGHT, dir[block_i].name, dir[block_i].day, getMonthName(dir[block_i].month), 1900 + dir[block_i].year, type_str);
  }

  qsort(content, n_entries, sizeof(char *), cstr_cmp);

  for(int i = 0; i < n_entries; i++)
    printf("%s\n", content[i]);

  for(int i = 0; i < n_entries; i++)
    free(content[i]);
  free(content);

  return;
}


// mkdir dir - creates a subdirectory named dir in the current directory
void vfs_mkdir(char *nome_dir) {
  if(strlen(nome_dir) > MAX_NAME_LENGHT) {
    printf("ERROR(mkdir: cannot create directory '%s' - name too long (MAX: %d characters))\n", nome_dir, MAX_NAME_LENGHT);
    return;
  }

  dir_entry *dir = (dir_entry *)BLOCK(current_dir);
  int n_entries = dir[0].size;

  if((n_entries % DIR_ENTRIES_PER_BLOCK == 0) + 1 > sb->n_free_blocks) {
    printf("ERROR(mkdir: cannot create directory '%s' - disk is full)\n", nome_dir);
    return;
  }

  if(find_dir_entry(dir, nome_dir) != NULL) {
    printf("ERROR(mkdir: cannot create directory '%s' - entry exists)\n", nome_dir);
    return;
  }

  dir[0].size++;

  int new_block = get_free_block();
  init_dir_block(new_block, current_dir);

  int cur_block = current_dir;
  while(fat[cur_block] != -1)
    cur_block = fat[cur_block];

  if(n_entries % DIR_ENTRIES_PER_BLOCK == 0) {
    int next_block = get_free_block();
    fat[cur_block] = next_block;
    cur_block = next_block;
  }

  dir = (dir_entry *)BLOCK(cur_block);
  
  init_dir_entry(&dir[n_entries % DIR_ENTRIES_PER_BLOCK], TYPE_DIR, nome_dir, 0, new_block);

  return;
}


// cd dir - move current directory to dir
void vfs_cd(char *nome_dir) {
  dir_entry *aux_dir_entry = find_dir_entry((dir_entry *)BLOCK(current_dir), nome_dir);

  if(aux_dir_entry == NULL){
    printf("ERROR(cd: cannot cd into '%s' - entry doesn't exist)\n", nome_dir);
    return;
  }

  current_dir = aux_dir_entry->first_block;

  return;
}


// pwd - writes the absolute path of the current directory
void vfs_pwd(void) {
  char name[1024], tmp[1024];

  name[0] = '/';
  name[1] = '\0';

  int tmp_dir = current_dir;

  while(tmp_dir != 0) {
    dir_entry *dir = (dir_entry *)BLOCK(tmp_dir);
    
    int prev_dir = dir[1].first_block;
    dir = (dir_entry *)BLOCK(prev_dir);
    
    int n_entries = dir[0].size;

    int cur_block = prev_dir;
    for(int i = 0; i < n_entries; i++) {
      if(i % DIR_ENTRIES_PER_BLOCK == 0 && i) {
        cur_block = fat[cur_block];
        dir = (dir_entry *)BLOCK(cur_block);
      }

      int block_i = i % DIR_ENTRIES_PER_BLOCK;

      if(dir[block_i].first_block == tmp_dir) {
        strcpy(tmp, dir[block_i].name);
        strcat(tmp, name);
        strcpy(name, tmp);
        strcpy(tmp, "/");
        strcat(tmp, name);
        strcpy(name, tmp);
        break;
      }
    }

    tmp_dir = prev_dir;
  }

  printf("%s\n", name);

  return;
}


// rmdir dir - removes the dir subdirectory (if empty) from the current directory
void vfs_rmdir(char *nome_dir) {
  dir_entry *dir = (dir_entry *)BLOCK(current_dir);
  int n_entries = dir[0].size;

  int cur_block = current_dir;
  int before_last_block = cur_block;
  for(int i = 0; i < n_entries; i++) {
    if(i % DIR_ENTRIES_PER_BLOCK == 0 && i) {
      before_last_block = cur_block;
      cur_block = fat[cur_block];
      dir = (dir_entry *)BLOCK(cur_block);
    }

    int block_i = i % DIR_ENTRIES_PER_BLOCK;

    if(!strcmp(dir[block_i].name, nome_dir)) {
      if(dir[block_i].type != TYPE_DIR) {
        printf("ERROR(rmdir: cannot remove directory '%s' - entry not a directory)\n", nome_dir);
        return;
      }

      dir_entry *del_dir = (dir_entry *)BLOCK(dir[block_i].first_block);

      if(del_dir[0].size != 2) {
        printf("ERROR(rmdir: cannot remove directory '%s' - entry not empty)\n", nome_dir);
        return;
      }

      int last_block = cur_block;
      while(fat[last_block] != -1)
        last_block = fat[last_block];

      dir_entry *last_dir_block = (dir_entry *)BLOCK(last_block);
      dir_entry last_dir = last_dir_block[(n_entries - 1 + DIR_ENTRIES_PER_BLOCK) % DIR_ENTRIES_PER_BLOCK];

      if((n_entries - 1) % DIR_ENTRIES_PER_BLOCK == 0) {
        free_block(last_block);
        fat[before_last_block] = -1;
      }
      
      free_block(dir[block_i].first_block);

      dir[block_i].type = last_dir.type;
      strcpy(dir[block_i].name, last_dir.name);
      dir[block_i].day = last_dir.day;
      dir[block_i].month = last_dir.month;
      dir[block_i].year = last_dir.year;
      dir[block_i].size = last_dir.size;
      dir[block_i].first_block = last_dir.first_block;

      dir = (dir_entry *)BLOCK(current_dir);
      dir[0].size--;

      return;
    }
  }

  printf("ERROR(rmdir: cannot remove directory '%s' - entry doesn't exist)\n", nome_dir);

  return;
}


// get file1 file2 - copies a standard UNIX file file1 to a file in our system file2
void vfs_get(char *nome_orig, char *nome_dest) {
  dir_entry *dir = (dir_entry *)BLOCK(current_dir);
  int n_entries = dir[0].size;

  if(find_dir_entry((dir_entry *)BLOCK(current_dir), nome_dest) != NULL) {
    printf("ERROR(get: cannot get '%s' - destination file already exists)\n", nome_dest);
    return;
  }

  struct stat statbuf;
  if(stat(nome_orig, &statbuf) == -1) {
    printf("ERROR(get: cannot get '%s' - input file not found)\n", nome_orig);
    return;
  }

  if((statbuf.st_mode & S_IFMT) != S_IFREG) {
    printf("ERROR(get: cannot get '%s' - file is not a regular file)\n", nome_orig);
    return;
  }
  
  int req_size = (int)statbuf.st_size;
  int req_blocks = (n_entries % DIR_ENTRIES_PER_BLOCK == 0) + (req_size + sb->block_size - 1) / sb->block_size;

  if(sb->n_free_blocks < req_blocks) {
    printf("ERROR(get: cannot get '%s' - disk space is full)\n", nome_orig);
    return;
  }

  req_blocks -= (n_entries % DIR_ENTRIES_PER_BLOCK == 0);

  dir[0].size++;

  int first_block = get_free_block();
  int new_block, next_block = first_block;
  int f_input = open(nome_orig, O_RDONLY), count_block = 1, n;
  char msg[4096];

  while((n = read(f_input, msg, sb->block_size)) > 0) {
    if(count_block != req_blocks) {
      count_block++;
      new_block = get_free_block();
      fat[next_block] = new_block;
    }

    strcpy(BLOCK(next_block), msg);

    next_block = new_block;
  }

  int cur_block = current_dir;
  while(fat[cur_block] != -1)
    cur_block = fat[cur_block];

  if(n_entries % DIR_ENTRIES_PER_BLOCK == 0) {
    int next_block = get_free_block();
    fat[cur_block] = next_block;
    cur_block = next_block;
  }

  dir = (dir_entry *)BLOCK(cur_block);
  init_dir_entry(&dir[n_entries % DIR_ENTRIES_PER_BLOCK], TYPE_FILE, nome_dest, req_size, first_block);

  close(f_input);
  
  return;
}


// put file1 file2 - copy a file from our system file1 to a normal UNIX file file2
void vfs_put(char *nome_orig, char *nome_dest) {
  dir_entry *dir = (dir_entry *)BLOCK(current_dir);
  int n_entries = dir[0].size;

  int cur_block = current_dir;
  for(int i = 0; i < n_entries; i++) {
    if(i % DIR_ENTRIES_PER_BLOCK == 0 && i) {
      cur_block = fat[cur_block];
      dir = (dir_entry *)BLOCK(cur_block);
    }

    int block_i = i % DIR_ENTRIES_PER_BLOCK;
        
    if(strcmp(dir[block_i].name, nome_orig) == 0) {
      if(dir[block_i].type != TYPE_FILE) {
        printf("ERROR(put: cannot put '%s' - entry not a file)\n", nome_orig);
        return;
      }

      int f_output = open(nome_dest, O_CREAT|O_TRUNC|O_WRONLY, 0644);
      int cur = dir[block_i].first_block, size = dir[block_i].size;

      write(f_output, BLOCK(cur), sb->block_size);
      size -= sb->block_size;
      while(fat[cur] != -1) {
        cur = fat[cur];
        if(size >= sb->block_size)
          write(f_output, BLOCK(cur), sb->block_size);
        else
          write(f_output, BLOCK(cur), size);

        size -= sb->block_size;
      }

      close(f_output);

      return;
    }
  }

  printf("ERROR(put: cannot put '%s' - file not found)\n", nome_orig);

  return;
}


// cat file - writes the contents of the file file to the screen
void vfs_cat(char *nome_fich) {
  dir_entry *dir = (dir_entry *)BLOCK(current_dir);
  int n_entries = dir[0].size;

  int cur_block = current_dir;
  for(int i = 0; i < n_entries; i++) {
    if(i % DIR_ENTRIES_PER_BLOCK == 0 && i) {
      cur_block = fat[cur_block];
      dir = (dir_entry *)BLOCK(cur_block);
    }

    int block_i = i % DIR_ENTRIES_PER_BLOCK;
        
    if(strcmp(dir[block_i].name, nome_fich) == 0) {
      if(dir[block_i].type != TYPE_FILE) {
        printf("ERROR(cat: cannot cat '%s' - entry not a file)\n", nome_fich);
        return;
      }

      int next_block = dir[block_i].first_block;
      write(1, BLOCK(next_block), sb->block_size);

      while(fat[next_block] != -1) {
        next_block = fat[next_block];

        int write_size = sb->block_size;
        if(fat[next_block] == -1)
          write_size = dir[block_i].size % sb->block_size;
  
        write(1, BLOCK(next_block), write_size);
      }

      return;
    }
  }

  printf("ERROR(cat: cannot cat '%s' - entry not found)\n", nome_fich);

  return;
}


// cp file1 file2 - copy the file file1 to file2
// cp file dir - copy the file file to the dir subdirectory
void vfs_cp(char *nome_orig, char *nome_dest) {
  dir_entry *dir = (dir_entry *)BLOCK(current_dir);
  int n_entries = dir[0].size, input_block = -1, exp_dir = current_dir;

  int block_i;
  int cur_block = current_dir;
  for(int i = 0; i < n_entries; i++) {
    if(i % DIR_ENTRIES_PER_BLOCK == 0 && i) {
      cur_block = fat[cur_block];
      dir = (dir_entry *)BLOCK(cur_block);
    }

    block_i = i % DIR_ENTRIES_PER_BLOCK;
        
    if(strcmp(dir[block_i].name, nome_orig) == 0) {
      input_block = dir[block_i].first_block;
      break;
    }
  }

  if(input_block == -1) {
    printf("ERROR(cp: cannot copy '%s' - file not found)\n", nome_orig);
    return;
  }

  int req_size = dir[block_i].size;

  dir = (dir_entry *)BLOCK(current_dir);
  cur_block = current_dir;
  for(int i = 0; i < n_entries; i++) {
    if(i % DIR_ENTRIES_PER_BLOCK == 0 && i) {
      cur_block = fat[cur_block];
      dir = (dir_entry *)BLOCK(cur_block);
    }

    block_i = i % DIR_ENTRIES_PER_BLOCK;
        
    if(strcmp(dir[block_i].name, nome_dest) == 0) {
      if(dir[block_i].type == TYPE_DIR) {
        exp_dir = dir[block_i].first_block;
        strcpy(nome_dest, nome_orig);
      }
      else{
        printf("Overwriting existing file with name %s\n", nome_dest);
        vfs_rm(nome_dest);
      }

      break;
    }
  }

  dir_entry *cur_dir = (dir_entry *)BLOCK(exp_dir);
  n_entries = cur_dir[0].size;
  int req_blocks = (n_entries % DIR_ENTRIES_PER_BLOCK == 0) + (req_size + sb->block_size - 1) / sb->block_size;

  if(sb->n_free_blocks < req_blocks) {
    printf("ERROR(cp: cannot copy '%s' - disk space is full)\n", nome_orig);
    return;
  }

  req_blocks -= (n_entries % DIR_ENTRIES_PER_BLOCK == 0);

  cur_dir[0].size++;

  int first_block = get_free_block();
  int new_block, next_block = first_block;
  int count_block = 1, cur = input_block;

  int tmp_req_size = req_size;
  if(tmp_req_size >= sb->block_size)
    strncpy(BLOCK(next_block), BLOCK(cur), sb->block_size);
  else
    strncpy(BLOCK(next_block), BLOCK(cur), tmp_req_size);
  while(fat[cur] != -1) {
    tmp_req_size -= sb->block_size;
    if(count_block != req_blocks) {
      count_block++;
      new_block = get_free_block();
      fat[next_block] = new_block;
    }

    cur = fat[cur];
    next_block = new_block;

    if(tmp_req_size >= sb->block_size)
      strncpy(BLOCK(next_block), BLOCK(cur), sb->block_size);
    else
      strncpy(BLOCK(next_block), BLOCK(cur), tmp_req_size);
  }

  cur_block = exp_dir;
  while(fat[cur_block] != -1)
    cur_block = fat[cur_block];

  if(n_entries % DIR_ENTRIES_PER_BLOCK == 0) {
    int next_block = get_free_block();
    fat[cur_block] = next_block;
    cur_block = next_block;
  }

  dir = (dir_entry *)BLOCK(cur_block);
  init_dir_entry(&dir[n_entries % DIR_ENTRIES_PER_BLOCK], TYPE_FILE, nome_dest, req_size, first_block);

  return;
}


// mv file1 file2 - move file from file1 to file2
// mv file dir - move the file file to the dir dir
void vfs_mv(char *nome_orig, char *nome_dest) {
  dir_entry *dir = (dir_entry *) BLOCK(current_dir);
  int n_entries = dir[0].size, i, inp_block = -1, exp_dir = current_dir, req_size = -1;

  int block_i;
  int cur_block = current_dir;
  for(i = 0; i < n_entries; i++) {
    if(i % DIR_ENTRIES_PER_BLOCK == 0 && i) {
      cur_block = fat[cur_block];
      dir = (dir_entry *) BLOCK(cur_block);
    }

    block_i = i % DIR_ENTRIES_PER_BLOCK;
        
    if(strcmp(dir[block_i].name, nome_orig) == 0) {
      int last_block = cur_block;
      
      while(fat[last_block] != -1)
        last_block = fat[last_block];

      dir_entry *last_dir_block = (dir_entry *)BLOCK(last_block);
      dir_entry last_dir = last_dir_block[(n_entries - 1 + DIR_ENTRIES_PER_BLOCK) % DIR_ENTRIES_PER_BLOCK];

      if((n_entries - 1 + DIR_ENTRIES_PER_BLOCK) % DIR_ENTRIES_PER_BLOCK == 0)
        free_block(last_block);

      req_size = dir[block_i].size;

      dir[block_i].type = last_dir.type;
      strcpy(dir[block_i].name, last_dir.name);
      dir[block_i].day = last_dir.day;
      dir[block_i].month = last_dir.month;
      dir[block_i].year = last_dir.year;
      dir[block_i].size = last_dir.size;
      dir[block_i].first_block = last_dir.first_block;

      dir = (dir_entry *)BLOCK(current_dir);
      dir[0].size--;
      
      inp_block = dir[block_i].first_block;
      break;
    }
  }

  if(inp_block == -1) {
    printf("ERROR(mv: cannot move '%s' - file not found)\n", nome_orig);
    return;
  }

  dir = (dir_entry *)BLOCK(current_dir);
  cur_block = current_dir;
  for(i = 0; i < n_entries; i++) {
    if(i % DIR_ENTRIES_PER_BLOCK == 0 && i) {
      cur_block = fat[cur_block];
      dir = (dir_entry *)BLOCK(cur_block);
    }

    block_i = i % DIR_ENTRIES_PER_BLOCK;
        
    if(strcmp(dir[block_i].name, nome_dest) == 0) {
      if(dir[block_i].type == TYPE_DIR) {
        exp_dir = dir[block_i].first_block;
        strcpy(nome_dest, nome_orig);
      }
      else
        vfs_rm(nome_dest);

      break;
    }
  }

  dir_entry *cur_dir = (dir_entry *)BLOCK(exp_dir);
  n_entries = cur_dir[0].size;
  cur_dir[0].size++;

  cur_block = exp_dir;
  while(fat[cur_block] != -1)
    cur_block = fat[cur_block];

  if(n_entries % DIR_ENTRIES_PER_BLOCK == 0) {
    int next_block = get_free_block();
    fat[cur_block] = next_block;
    cur_block = next_block;
  }

  dir = (dir_entry *)BLOCK(cur_block);
  init_dir_entry(&dir[n_entries % DIR_ENTRIES_PER_BLOCK], TYPE_FILE, nome_dest, req_size, inp_block);

  return;
}


// rm file - removes the file file
void vfs_rm(char *nome_fich) {
  dir_entry *dir = (dir_entry *)BLOCK(current_dir);
  int n_entries = dir[0].size, i;

  int cur_block = current_dir;
  for(i = 0; i < n_entries; i++) {
    if(i % DIR_ENTRIES_PER_BLOCK == 0 && i) {
      cur_block = fat[cur_block];
      dir = (dir_entry *)BLOCK(cur_block);
    }

    int block_i = i % DIR_ENTRIES_PER_BLOCK;
        
    if(dir[block_i].type == TYPE_FILE && strcmp(dir[block_i].name, nome_fich) == 0) {
      int next_block = dir[block_i].first_block, count = 1;

      while(fat[next_block] != -1) {
        next_block = fat[next_block];
        count++;
      }

      sb->n_free_blocks += count;
      fat[next_block] = sb->free_block;
      sb->free_block = dir[block_i].first_block;

      int last_block = cur_block;

      while(fat[last_block] != -1)
        last_block = fat[last_block];
      
      dir_entry *last_dir_block = (dir_entry *)BLOCK(last_block);
      dir_entry last_dir = last_dir_block[(n_entries - 1 + DIR_ENTRIES_PER_BLOCK) % DIR_ENTRIES_PER_BLOCK];

      if((n_entries - 1 + DIR_ENTRIES_PER_BLOCK) % DIR_ENTRIES_PER_BLOCK == 0)
        free_block(last_block);

      dir[block_i].type = last_dir.type;
      strcpy(dir[block_i].name, last_dir.name);
      dir[block_i].day = last_dir.day;
      dir[block_i].month = last_dir.month;
      dir[block_i].year = last_dir.year;
      dir[block_i].size = last_dir.size;
      dir[block_i].first_block = last_dir.first_block;

      dir = (dir_entry *)BLOCK(current_dir);
      dir[0].size--;

      return;
    }
  }

  printf("ERROR(rm: cannot remove '%s' - file not found)\n", nome_fich);
  
  return;
}
