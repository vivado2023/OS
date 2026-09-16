#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

void find(char *path, char *name)
{
  char buf[512], *p;
  int fd;
  struct stat st;
  struct dirent de;

  if ((fd = open(path, 0)) < 0) {
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }
  if (fstat(fd, &st) < 0) {
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }

  p = path + strlen(path);
  while (p > path && *(p - 1) != '/')
    p--;
  if ((st.type == T_FILE || st.type == T_DIR) && strcmp(p, name) == 0)
    printf("%s\n", path);

  if (st.type == T_DIR) {
    if (strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf)) {
      fprintf(2, "find: path too long\n");
      close(fd);
      return;
    }
    strcpy(buf, path);
    p = buf + strlen(buf);
    if (p > buf && *(p - 1) != '/')
      *p++ = '/';

    while (read(fd, &de, sizeof(de)) == sizeof(de)) {
      if (de.inum == 0)
        continue;

      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0;
      if (strcmp(p, ".") == 0 || strcmp(p, "..") == 0)
        continue;

      find(buf, name);
    }
  }
  close(fd);
}

int main(int argc, char *argv[])
{
  if (argc != 3) 
    exit(1);

  int len = strlen(argv[1]);
  while (len > 1 && argv[1][len - 1] == '/')
    argv[1][--len] = 0;

  find(argv[1], argv[2]);
  exit(0);
}
