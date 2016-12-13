#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <grp.h>
#include <time.h>
#include <pwd.h>

#define MODE_STR_LEN 10

typedef struct {
   int full;
   int all;
   int recurse;
} Options;

typedef struct {
   int flag;    // Mode bitflag to check in st_mode
   int index;   // Offset with long-listing string that corresponds
   int val;     // Letter to use if the bit is "true"
} ModeChar;

const ModeChar modeChrs[] = {
   {S_IRUSR, 1, 'r'}, {S_IWUSR, 2, 'w'}, {S_IXUSR, 3, 'x'},
   {S_IRGRP, 4, 'r'}, {S_IWGRP, 5, 'w'}, {S_IXGRP, 6, 'x'},
   {S_IROTH, 7, 'r'}, {S_IWOTH, 8, 'w'}, {S_IXOTH, 9, 'x'},
   {S_ISUID, 3, 's'}, {S_ISGID, 6, 's'}, {S_IFDIR, 0, 'd'},
   {0, 0, 0}
};

typedef struct stat Stats;      // Fix dumb old declarations
typedef struct dirent DirEntry;

void writeStats(int indent, char *name, Stats *stats) {
   char modeStr[MODE_STR_LEN+1], *timeStr;
   const ModeChar *modeChr;
   int m;

   printf("%*s", indent, "");

   if (!stats)
      printf("%s\n", name);
   else {
      for (m = 0; m < MODE_STR_LEN; m++)
         modeStr[m] = '-';
      modeStr[m] = 0;
	  
      for (modeChr = modeChrs; modeChr->flag; modeChr++)
         if (stats->st_mode & modeChr->flag)
            modeStr[modeChr->index] = modeChr->val;
      
      timeStr = asctime(gmtime(&stats->st_mtime));
      timeStr[strlen(timeStr)-1] = 0;  // Drop stupid return. 
	  
      printf("%s %s %s %9lu %s %s\n", modeStr,
       getpwuid(stats->st_uid)->pw_name, getgrgid(stats->st_gid)->gr_name, 
       (unsigned long)stats->st_size, timeStr, name);
   }
}

// List fileName directly, if a file, or list its contents if a dir
int list(int indent, char *fileName, Options opts) {
   Stats fileStats;
   DIR *dir;
   DirEntry *dirEntry;
   int ndx;
   
   if (!lstat(fileName, &fileStats)) { 
      if (S_ISREG(fileStats.st_mode))
         writeStats(indent, fileName, opts.full ? &fileStats : NULL);
      if (S_ISDIR(fileStats.st_mode) && (dir = opendir(fileName))) {
         chdir(fileName);
         for (ndx = 0; NULL != (dirEntry = readdir(dir));) {
            lstat(dirEntry->d_name, &fileStats);
            if (opts.all || *dirEntry->d_name != '.') {
               writeStats(indent, dirEntry->d_name, opts.full ? &fileStats : NULL);
               if (opts.recurse && S_ISDIR(fileStats.st_mode))
                  list(indent+3, dirEntry->d_name, opts);
            }
         }
         chdir("..");
         closedir(dir);
      }
   }
}

int main(int argc, char **argv)
{
   Options opts = {0, 0, 0};
   char *arg, flag;
   int fileArgs = 0;
   
   for (argc--, argv++; argc; argc--, argv++) {
      arg = *argv;
      if (*arg++ == '-')
         while (flag = *arg++) {
            opts.all = opts.all || flag == 'a';
            opts.full = opts.full || flag == 'l';
            opts.recurse = opts.recurse || flag == 'R';
         }
      else {
         fileArgs++;
         list(0, *argv, opts);
      }		 
   }
   if (!fileArgs)
      list(0, ".", opts);
}
