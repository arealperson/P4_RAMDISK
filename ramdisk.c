/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
  Copyright (C) 2011       Sebastian Pipping <sebastian@pipping.org>

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.

  gcc -Wall ramdisk.c `pkg-config fuse --cflags --libs` -o ramdisk

  gcc -std=c99 -Wall -pedantic postmark.c -o postmark

  ./ramdisk /tmp/fuse/ -f 512 /home/venkatesh/ram.files

  ./ramdisk /tmp/fuse/ 512 /home/vsamban/ram.txt

  xmllint –format ugly.xml –output pretty.xml
*/

#define FUSE_USE_VERSION 26


#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#include <stdlib.h>

struct fileSystem{
	char *filePath;
	struct filesStruct *head;
};

struct filesStruct{
	int isfile;
	char *name;
	char *content;
	mode_t mode;
	struct filesStruct *subdir;
	struct filesStruct *next;
};

struct fileSystem *ramfiles = NULL;
char *mountPoint;
void createFile(const char *path);
signed int totalMemory;
int saveFile = -1;
int writeMemory = 0;
int debug = 0;

static int xmp_getattr(const char *path, struct stat *stbuf)
{
	struct fileSystem *tempFiles = NULL;
	struct filesStruct *dirList= NULL;
	/*printf("Getattr filepath %s\n",path);*/
	int response = -ENOENT;
	tempFiles = ramfiles;
	dirList = tempFiles->head;
	int found = -1;
	char *tempPath = strndup(path,strlen(path));
	char *pathPtr;
	if(strcmp(path,"/") == 0){
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
		response =0;
	}else{
		pathPtr = strtok(tempPath,"/");
		while(pathPtr != NULL){
			while(dirList!= NULL){
				if(strcmp(dirList->name,pathPtr) == 0){
					found = 1;
					break;
				}
				dirList = dirList->next;
			}
			pathPtr = strtok(NULL,"/");
			if(found == 1){
				if(dirList->isfile){
					stbuf->st_mode = S_IFREG | 0666;
					stbuf->st_nlink = 1;
					/*printf("File Name %s\n",dirList->name);*/
					if(dirList->content != NULL)
						stbuf->st_size = strlen(dirList->content);
				}else{
					stbuf->st_mode = S_IFDIR | 0755;
					stbuf->st_nlink = 2;
				}
				response = 0;
				if(pathPtr != NULL){
					found = -1;
					dirList = dirList->subdir;
					response = -ENOENT;
				}else{
					break;
				}
			}
			else{
				response = -ENOENT;
			}
		}
	}
	/*printf("Returning getattr %d\n",response);*/
	free(tempPath);
	return response;
}

static int ram_opendir(const char *path, struct fuse_file_info *fi)
{

	return 0;
}

static int xmp_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		       off_t offset, struct fuse_file_info *fi)
{
	/*printf("<<r>> readdir called with path %s\n",path);*/
	int found =0;
	char *printFile, *dirPath, *pathPtr;
	struct fileSystem *tempFiles = ramfiles ;
	struct filesStruct *dirList;
	dirList = tempFiles->head;
	dirPath = strndup(path,strlen(path));
	if(strcmp(path,"/") == 0){
		if(dirList == NULL){

		}else{
			while(dirList != NULL){
				printFile = strndup(dirList->name,strlen(dirList->name));
				filler(buf, printFile, NULL, 0);
				free(printFile);
				dirList = dirList->next;
			}
		}
	}else{
		pathPtr = strtok(dirPath,"/");
		while(pathPtr != NULL){
			while(dirList != NULL){
				if(strcmp(dirList->name,pathPtr) == 0){
					found = 1;
					break;
				}
				dirList = dirList->next;
			}
			pathPtr = strtok(NULL,"/");
			if(found == 1){
				dirList = dirList->subdir;
				if(pathPtr != NULL){
					found = -1;
				}else
					break;
			}
		}
		while(dirList != NULL){
			printFile = strndup(dirList->name,strlen(dirList->name));
			filler(buf, printFile, NULL, 0);
			dirList = dirList->next;
			free(printFile);
		}
	}
	/*printf("\t\tReaddir returned\n");*/
	free(dirPath);
	return 0;
}

static int xmp_mkdir(const char *path, mode_t mode)
{
	/*printf("<<m>> Path in mkdir is %s\n",path);*/
	char *dirPath, *pathPtr, *dirName;
	struct filesStruct *parent;
	int i,charcount = 0,  found = -1;
	dirPath = strndup(path,strlen(path));
	struct filesStruct *dirList = ramfiles->head;
	int dirSize = sizeof(struct filesStruct);
	if((totalMemory - dirSize) < 0){
		fprintf(stderr,"%s","NO ENOUGH SPACE");
		return -ENOSPC;
		/*exit(-ENOSPC);*/
	}
	struct filesStruct *addDir = (struct filesStruct *)malloc(sizeof(struct filesStruct));
	addDir->isfile = 0;;
	addDir->next = NULL;
	addDir->subdir = NULL;
	addDir->mode = mode;
	for(i=0;i<strlen(dirPath); i++) {
		    if(dirPath[i] == '/') {
		        charcount ++;
		    }
		}
	pathPtr = strtok(dirPath,"/");
	if(charcount == 1){
		addDir->name = strndup(pathPtr,strlen(pathPtr));
		if(dirList == NULL){
				ramfiles->head = addDir;
			}else{
				while(dirList->next != NULL)
					dirList = dirList->next;
				dirList->next = addDir;
			}
	}else{
		while(pathPtr != NULL){
			while(dirList != NULL){
				if(strcmp(dirList->name,pathPtr) == 0){
					found = 1;
					break;
				}
				dirList = dirList->next;
			}
			pathPtr = strtok(NULL,"/");
			if(found == 1){
				if(pathPtr != NULL){
					found = -1;
					dirName = pathPtr;
					parent = dirList;
					dirList = dirList->subdir;
				}else
					break;
			}else{
				/*printf("<<m>> Current directory %s\n",parent->name);
				printf("<<m>> New directory %s\n",dirName);*/
				dirList = parent->subdir;
				addDir->name = strdup(dirName);
				if(dirList == NULL){
					parent->subdir = addDir;
				}else{
					while(dirList->next != NULL)
						dirList = dirList->next;
					dirList->next = addDir;
				}
			}
		}
	}
	/*printf("<make> Directory created\n");*/
	dirSize = dirSize + strlen(addDir->name);
	/*printf("Total memory in mkdir before %d\n",totalMemory);*/
	totalMemory = totalMemory - dirSize;
	/*printf("Total memory in mkdir after%d\n\n",totalMemory);*/
	free(dirPath);
	return 0;
}

static int xmp_unlink(const char *path)
{
	/*printf(">>>>>> Unlink was called with %s \n",path);*/

	char *dirPath, *pathPtr;
	struct filesStruct *prev, *parent = NULL;
	prev = NULL;
	int i,charcount = 0,  found = -1;
	dirPath = strndup(path,strlen(path));
	struct filesStruct *dirList = prev = ramfiles->head;
	for(i=0;i<strlen(dirPath); i++) {
		    if(dirPath[i] == '/') {
		        charcount ++;
		    }
		}
	pathPtr = strtok(dirPath,"/");
	if(charcount == 1){
		while(dirList->next != NULL){
			if(strcmp(dirList->name,pathPtr) == 0 )
				break;
			prev = dirList;
			dirList = dirList->next;
		}
		/*printf("The prev unlink has filename is %s\n",prev->name);
		printf("The unlink has filename is %s\n",dirList->name);*/
		if(strcmp(prev->name,dirList->name) == 0){
			ramfiles->head = dirList->next;
		}else{
			prev->next = dirList->next;
		}
	}else{
		while(pathPtr != NULL){
			while(dirList != NULL){
				if(strcmp(dirList->name,pathPtr) == 0){
					found = 1;
					break;
				}
				prev = dirList;
				dirList = dirList->next;
			}
			pathPtr = strtok(NULL,"/");
			if(found == 1){
				if(pathPtr != NULL){
					found = -1;
					parent = dirList;
					dirList = dirList->subdir;
					prev = dirList;
				}else
					break;
			}
		}
		/*printf("The prev unlink has filename is %s\n",prev->name);
		printf("The unlink has filename is %s\n",dirList->name);*/
		if(strcmp(prev->name,dirList->name) == 0){
			parent->subdir = dirList->next;
		}else{
			prev->next = dirList->next;
		}
	}
	/*printf("The prev unlink has filename is %s\n",prev->name);
	printf("The unlink has filename is %s\n",dirList->name);*/
	int contentSize = 0;
	if(dirList->content != NULL)
		contentSize = strlen(dirList->content);
	int dirSize = sizeof(struct filesStruct) + strlen(dirList->name)+ contentSize;
	/*printf("Total memory before unlink %d\n",totalMemory);*/
	totalMemory = totalMemory + dirSize;
	/*printf("UNLINK: Total memory after unlink %d\n",totalMemory);*/
	if(dirList->content != NULL)
		free(dirList->content);
	free(dirList->name);
	free(dirList);
	return 0;
}

static int xmp_rmdir(const char *path){
	/*printf("Remove was called with path %s\n",path);*/

	char *dirPath, *pathPtr;
	struct filesStruct *prev, *parent = NULL;
	prev = NULL;
	int i,charcount = 0,  found = -1;
	dirPath = strndup(path,strlen(path));
	struct filesStruct *dirList = prev = ramfiles->head;
	for(i=0;i<strlen(dirPath); i++) {
		    if(dirPath[i] == '/') {
		        charcount ++;
		    }
		}
	pathPtr = strtok(dirPath,"/");
	if(charcount == 1){
		while(dirList->next != NULL){
			if(strcmp(dirList->name,pathPtr) == 0 )
				break;
			prev = dirList;
			dirList = dirList->next;
		}
		if(dirList->subdir != NULL)
			return -EPERM;
		/*printf("The prev unlink has filename is %s\n",prev->name);
		printf("The unlink has filename is %s\n",dirList->name);*/
		if(strcmp(prev->name,dirList->name) == 0){
			ramfiles->head = dirList->next;
		}else{
			prev->next = dirList->next;
		}
	}else{
		while(pathPtr != NULL){
			while(dirList != NULL){
				if(strcmp(dirList->name,pathPtr) == 0){
					found = 1;
					break;
				}
				prev = dirList;
				dirList = dirList->next;
			}
			pathPtr = strtok(NULL,"/");
			if(found == 1){
				if(pathPtr != NULL){
					found = -1;
					parent = dirList;
					dirList = dirList->subdir;
					prev = dirList;
				}else
					break;
			}
		}
		if(dirList->subdir != NULL)
			return -ENOTEMPTY;

		/*printf("The prev unlink has filename is %s\n",prev->name);
		printf("The unlink has filename is %s\n",dirList->name);*/
		if(strcmp(prev->name,dirList->name) == 0){
			parent->subdir = dirList->next;
		}else{
			prev->next = dirList->next;
		}
	}
	/*printf("The dir to be removed is %s\n",dirList->name);*/
	int dirSize = sizeof(struct filesStruct) + strlen(dirList->name);
	/*printf("total memory before removing %d\n",totalMemory);*/
	totalMemory = totalMemory + dirSize;
	/*printf("RMDIR: total memory after removing %d\n\n",totalMemory);*/
	free(dirList->name);
	free(dirList);
	free(dirPath);
	return 0;
}

static int xmp_open(const char *path, struct fuse_file_info *fi)
{
	/*printf("<<o>> Open is Called with path %s\n",path);*/
	char *dirPath, *pathPtr;
	int i,charcount = 0,  found = -1;
	dirPath = strndup(path,strlen(path));
	struct filesStruct *dirList = ramfiles->head;
	for(i=0;i<strlen(dirPath); i++) {
		    if(dirPath[i] == '/') {
		        charcount ++;
		    }
		}

	pathPtr = strtok(dirPath,"/");
	if(charcount == 1){
		while(dirList->next != NULL){
			if(strcmp(dirList->name,pathPtr) == 0 ){
				found = 1;
				break;
			}
			dirList = dirList->next;
		}
	}else{
		while(pathPtr != NULL){
			while(dirList != NULL){
				if(strcmp(dirList->name,pathPtr) == 0){
					found = 1;
					break;
				}
				dirList = dirList->next;
			}
			pathPtr = strtok(NULL,"/");
			if(found == 1){
				if(pathPtr != NULL){
					found = -1;
					dirList = dirList->subdir;
				}else
					break;
			}
		}
	}
	free(dirPath);
	/*printf("\t\topen found %d\n",found);*/
	if(found)
		return 0;
	else
		return -ENOENT;
}
struct filesStruct *getReadNode(const char *path){
	char *dirPath, *pathPtr;
	int i,charcount = 0,  found = -1;
	dirPath = strdup(path);
	struct filesStruct *dirList = ramfiles->head;
	for(i=0;i<strlen(dirPath); i++) {
			if(dirPath[i] == '/') {
				charcount ++;
			}
		}

	pathPtr = strtok(dirPath,"/");
	if(charcount == 1){
		while(dirList != NULL){
			if(strcmp(dirList->name,pathPtr) == 0 ){
				found = 1;
				break;
			}
			dirList = dirList->next;
		}
	}else{
		while(pathPtr != NULL){
			while(dirList != NULL){
				if(strcmp(dirList->name,pathPtr) == 0){
					found = 1;
					break;
				}
				dirList = dirList->next;
			}
			pathPtr = strtok(NULL,"/");
			if(found == 1){
				if(pathPtr != NULL){
					found = -1;
					dirList = dirList->subdir;
				}else
					break;
			}
		}
	}
	free(dirPath);
	return dirList;
}
static int xmp_read(const char *path, char *buf, size_t size, off_t offset,
		    struct fuse_file_info *fi)
{
	/*printf("<<r>> Read is Called with path %s\n",path);*/
	int i = 0;
	struct filesStruct *dirList= NULL;
	while (dirList == NULL){
		i++;
		dirList = getReadNode(path);
		if(i>20){
			/*printf(">>>Couldnt find %s\n",path);*/
			return 0;
		}
	}
	/*printf("The read file name is %s\n",dirList->name);
	printf("Read size is %d\n",strlen(dirList->content));
	printf("the offset is %d\n",offset);
	printf("<<r>> Requested size is %zu\n",size);*/
	/*int readBytes = 0;
	int readSize = 0;
	printf("Got Node\n");
	printf("Node name\t%s\n",dirList->name);*/
	if( dirList->content != NULL){
		/*readBytes = strlen(dirList->content);*/
		size_t len = strlen(dirList->content);
		if (offset < len) {
			if (offset + size > len)
				size = len - offset;
			memcpy(buf, dirList->content + offset, size);
			/*readSize = strlen(buf);*/
			buf[size] = '\0';
		} else
			size = 0;
	}else{
		size = 0;
	}
	/*printf("Returning from read %d\n",readSize);*/
	return size;
}

static int xmp_write(const char *path, const char *buf, size_t size,
		     off_t offset, struct fuse_file_info *fi){

	/*printf("<<m>> path is %s\n",path);*/
	char *dirPath, *pathPtr;
	int i,charcount = 0,  found = -1;
	dirPath = strndup(path,strlen(path));
	struct filesStruct *dirList = ramfiles->head;
	for(i=0;i<strlen(dirPath); i++) {
		    if(dirPath[i] == '/') {
		        charcount ++;
		    }
		}
	signed int ramSize = 0;
	/*ENOSPC was removed*/

	pathPtr = strtok(dirPath,"/");
	if(charcount == 1){
		while(dirList->next != NULL){
			if(strcmp(dirList->name,pathPtr) == 0 )
				break;
			dirList = dirList->next;
		}
	}else{
		while(pathPtr != NULL){
			while(dirList != NULL){
				if(strcmp(dirList->name,pathPtr) == 0){
					found = 1;
					break;
				}
				dirList = dirList->next;
			}
			pathPtr = strtok(NULL,"/");
			if(found == 1){
				if(pathPtr != NULL){
					found = -1;
					dirList = dirList->subdir;
				}else
					break;
			}
		}
	}
	/*printf("\t\t<<w>> The buf is %s\n",buf);
	printf("\t\t<<w>> offset %zu\n",offset);
	printf("\t\t<<w>> Size is %zu\n",size);*/

	if(dirList->content == NULL){
		signed int len = strlen(buf);
		ramSize = totalMemory - len;
		if(ramSize < 0){
			fprintf(stderr,"%s","NO ENOUGH SPACE");
			return -ENOSPC;
			/*exit(-ENOSPC);*/
		}
		char *temp = malloc(size+1);
		memset(temp,0,size+1);
		strncpy(temp,buf,size);
		temp[size] = '\0';
		dirList->content = temp;
		totalMemory = totalMemory - size;
		writeMemory = writeMemory + size;
		/*printf("NEW WRITE: Total memory after write %d\n",totalMemory);
		printf("Write memory in after write %d\t%s\n",writeMemory,path);*/
	}else {
		 int newSize = offset + size;
		 int len = strlen(dirList->content);

		if(newSize > len){
			int diff = newSize - len;
			ramSize = totalMemory - diff;

			if(ramSize < 0){
				/*printf("\t\t APPEND : NO ENOUGH SPACE \tramsize%d\ttoalmemory=%d\tnewsize=%d\t%dlen\n",ramSize,totalMemory,newSize,len);*/
				fprintf(stderr,"%s","NO ENOUGH SPACE");
				return -ENOSPC;
				/*exit(-ENOSPC);*/
			}
			/*totalMemory = totalMemory + len;
			writeMemory = writeMemory - len;*/
			char *temp = malloc(newSize+1);
			memset(temp,0,newSize+1);
			strncpy(temp,dirList->content,len);
			temp[len] = '\0';
			free(dirList->content);
			dirList->content = temp;
			 /*int curSize = strlen(dirList->content);
			printf("CURSIZE content %d\n",len);
			 int oldTotal = totalMemory;
			printf("OLD TOTAL: old total is %d\n",totalMemory);
			if(curSize < 0)
				totalMemory = totalMemory + curSize;
			else*/
			totalMemory = totalMemory + len - (offset+size);
			writeMemory = writeMemory + offset + size - len;
			/*printf("APPEND WRITE: Total memory after write %d\tramsize=%d\tdifference=%d\n",totalMemory,ramSize,diff);
			printf("Write memory in after write %d\t%s\n",writeMemory,path);*/
		}
		int i,j=0;
		for(i=offset;i<(offset+size);i++){
			dirList->content[i] = buf[j];
			j++;
		}
		dirList->content[i] = '\0';
	}
	/*printf("Total memory after write %d\n",totalMemory);*/


	free(dirPath);
	/*printf("\t\tReturning from write %zu\n",size);*/
	return size;
}

static int ram_truncate (const char * path , off_t offset){
	/*printf("truncate was called %s\n",path);*/
	return 0;
}

static int ram_create(const char *path , mode_t mode, struct fuse_file_info *fi){
	/*printf("Create was called \n");
	printf("<<c>> Mode is %d\n",mode);*/
	/*createFile(path);*/
	char *dirPath, *pathPtr, *dirName;

	struct filesStruct *parent;
	int i,charcount = 0,  found = -1;
	dirPath = strdup(path);
	/*printf("<<c>> Create path %s\n",dirPath);*/
	struct filesStruct *dirList = ramfiles->head;
	int dirSize = sizeof(struct filesStruct);
	/*printf("Check size\n");*/
	int diffSize = totalMemory - dirSize;
	/*printf("Different size %d",diffSize);*/
	if(diffSize < 0){
		fprintf(stderr,"%s","NO ENOUGH SPACE");
		return -ENOSPC;
		/*exit(-ENOSPC);*/
	}
	struct filesStruct *addFile = (struct filesStruct *)malloc(sizeof(struct filesStruct));
	addFile->isfile = 1;
	addFile->next = NULL;
	addFile->subdir = NULL;
	addFile->content = NULL;
	addFile->mode = mode;
	for(i=0;i<strlen(dirPath); i++) {
		    if(dirPath[i] == '/') {
		        charcount ++;
		    }
		}
	if(charcount == 1){
		pathPtr = strtok(dirPath,"/");
		addFile->name = strdup(pathPtr);
		if(dirList == NULL){
				ramfiles->head = addFile;
			}else{

				while(dirList->next != NULL)
					dirList = dirList->next;
				dirList->next = addFile;
			}
	}else{
		pathPtr = strtok(dirPath,"/");
		while(pathPtr != NULL){
			while(dirList != NULL){
				if(strcmp(dirList->name,pathPtr) == 0){
					found = 1;
					break;
				}
				dirList = dirList->next;
			}
			pathPtr = strtok(NULL,"/");
			if(found == 1){
				if(pathPtr != NULL){
					found = -1;
					dirName = pathPtr;
					parent = dirList;
					dirList = dirList->subdir;
				}else
					break;
			}else{
				/*printf("<<m>> Current directory %s\n",parent->name);
				printf("<<m>> New directory %s\n",dirName);*/
				dirList = parent->subdir;
				addFile->name = strdup(dirName);
				if(dirList == NULL){
					parent->subdir = addFile;
				}else{

					while(dirList->next != NULL)
						dirList = dirList->next;
					dirList->next = addFile;
				}
			}
		}
	}
	/*printf("Total memory in before create a%d\n",totalMemory);*/
	totalMemory = totalMemory - (dirSize + strlen(addFile->name));
	/*printf("CREATE: Total memory in after create %d\n",totalMemory);*/
	free(dirPath);
	return 0;
}

void createFile(const char *path){

	char *dirPath, *pathPtr, *dirName;

	struct filesStruct *parent;
	int i,charcount = 0,  found = -1;
	dirPath = strdup(path);
	struct filesStruct *dirList = ramfiles->head;
	struct filesStruct *addFile = (struct filesStruct *)malloc(sizeof(struct filesStruct));
	addFile->isfile = 1;
	addFile->next = NULL;
	addFile->subdir = NULL;
	addFile->content = NULL;
	for(i=0;i<strlen(dirPath); i++) {
		    if(dirPath[i] == '/') {
		        charcount ++;
		    }
		}
	pathPtr = strtok(dirPath,"/");
	if(charcount == 1){
		addFile->name = strndup(pathPtr,strlen(pathPtr));
		if(dirList == NULL){
				ramfiles->head = addFile;
			}else{
				while(dirList->next != NULL)
					dirList = dirList->next;
				dirList->next = addFile;
			}
	}else{
		while(pathPtr != NULL){
			while(dirList != NULL){
				if(strcmp(dirList->name,pathPtr) == 0){
					found = 1;
					break;
				}
				dirList = dirList->next;
			}
			pathPtr = strtok(NULL,"/");
			if(found == 1){
				if(pathPtr != NULL){
					found = -1;
					dirName = pathPtr;
					parent = dirList;
					dirList = dirList->subdir;
				}else
					break;
			}else{
				/*printf("<<m>> Current directory %s\n",parent->name);
				printf("<<m>> New directory %s\n",dirName);*/
				dirList = parent->subdir;
				addFile->name = strndup(dirName,strlen(dirName));
				if(dirList == NULL){
					parent->subdir = addFile;
				}else{
					while(dirList->next != NULL)
						dirList = dirList->next;
					dirList->next = addFile;
				}
			}
		}
	}
	free(dirPath);
}

void createDir(const char *path){

	char *dirPath, *pathPtr, *dirName;
	struct filesStruct *parent;
	int i,charcount = 0,  found = -1;
	dirPath = strdup(path);
	struct filesStruct *dirList = ramfiles->head;
	struct filesStruct *addDir = (struct filesStruct *)malloc(sizeof(struct filesStruct));
	addDir->isfile = 0;;
	addDir->next = NULL;
	addDir->subdir = NULL;
	for(i=0;dirPath[i]; i++) {
		if(dirPath[i] == '/') {
			charcount ++;
		}
	}
	pathPtr = strtok(dirPath,"/");
	if(charcount == 1){
		addDir->name = strndup(pathPtr,strlen(pathPtr));
		if(dirList == NULL){
				ramfiles->head = addDir;
			}else{
				while(dirList->next != NULL)
					dirList = dirList->next;
				dirList->next = addDir;
			}
	}else{
		while(pathPtr != NULL){
			while(dirList != NULL){
				if(strcmp(dirList->name,pathPtr) == 0){
					found = 1;
					break;
				}
				dirList = dirList->next;
			}
			pathPtr = strtok(NULL,"/");
			if(found == 1){
				if(pathPtr != NULL){
					found = -1;
					dirName = pathPtr;
					parent = dirList;
					dirList = dirList->subdir;
				}else
					break;
			}else{
				/*printf("<<m>> Current directory %s\n",parent->name);
				printf("<<m>> New directory %s\n",dirName);*/
				dirList = parent->subdir;
				addDir->name = strndup(dirName,strlen(dirName));
				if(dirList == NULL){
					parent->subdir = addDir;
				}else{
					/*Root already has files*/
					while(dirList->next != NULL)
						dirList = dirList->next;
					dirList->next = addDir;
				}
			}
		}
	}
	free(dirPath);
}

struct filesStruct *findPath(const char *source,int retPrev){
	char *dirPath, *pathPtr;
	struct filesStruct *prev, *parent = NULL;
	prev = NULL;
	int i,charcount = 0,  found = -1;
	dirPath = strdup(source);
	struct filesStruct *dirList = prev = ramfiles->head;
	for(i=0;dirPath[i]; i++) {
		if(dirPath[i] == '/') {
			charcount ++;
		}
	}
	pathPtr = strtok(dirPath,"/");
	if(charcount == 1){
		while(dirList->next != NULL){
			if(strcmp(dirList->name,pathPtr) == 0 )
				break;
			prev = dirList;
			dirList = dirList->next;
		}
		/*printf("The prev unlink has filename is %s\n",prev->name);
		printf("The unlink has filename is %s\n",dirList->name);*/
	}else{
		while(pathPtr != NULL){
			while(dirList != NULL){
				if(strcmp(dirList->name,pathPtr) == 0){
					found = 1;
					break;
				}
				prev = dirList;
				dirList = dirList->next;
			}
			pathPtr = strtok(NULL,"/");
			if(found == 1){
				if(pathPtr != NULL){
					found = -1;
					parent = dirList;
					dirList = dirList->subdir;
					prev = dirList;
				}else
					break;
			}
		}
		/*printf("The prev unlink has filename is %s\n",prev->name);
		printf("The unlink has filename is %s\n",dirList->name);*/
	}
	if(retPrev == 1)
		return prev;
	else if (retPrev == 0)
		return dirList;
	else if (retPrev == 2)
		return parent;
	return NULL;
}

static int ram_rename(const char *source, const char *dest){

	/*printf("rename was called \n");*/
	char *dirPath;
	int i,charcount = 0;
	dirPath = strdup(source);

	/*printf("Source path=%s\tdest path=%s\n",source,dest);*/
	struct filesStruct *dirList = findPath(source,0);
	struct filesStruct *prev = findPath(source,1);
	/*printf("found dirName is %s\n",dirList->name);
	printf("found prev is %s\n",prev->name);*/


	for(i=0;dirPath[i]; i++) {
		if(dirPath[i] == '/') {
			charcount ++;
		}
	}
	if(charcount == 1){
		if(strcmp(prev->name,dirList->name) == 0){
			ramfiles->head = dirList->next;
		}else{
			prev->next = dirList->next;
		}
	}else{
		if(strcmp(prev->name,dirList->name) == 0){
			struct filesStruct *parent = findPath(source,2);
			parent->subdir = dirList->next;
		}else{
			prev->next = dirList->next;
		}
	}
	dirList->next = NULL;
	if(dirList->isfile == 0){
		createDir(dest);
	}else{
		createFile(dest);
	}

	/*dirPath = strdup(dest);*/
	struct filesStruct *destDirList = findPath(dest,0);
	struct filesStruct *destPrev = findPath(dest,1);
	/*printf("\n\tfound dest dirName is %s\n",destDirList->name);
	printf("\n\tfound dest prev is %s\n\n",destPrev->name);*/

	if(dirList->isfile == 0){
		if(strcmp(destPrev->name,destDirList->name)==0){
			struct filesStruct *dirparent = findPath(dest,2);
			if(dirparent == NULL){
				printf("ISNULL \n");
				ramfiles->head = dirList;
				dirList->name = strdup(destDirList->name);
				dirList->next = destDirList->next;
			}else{
				printf("name=%s\n",dirparent->name);
				dirparent->subdir = dirList;
				dirList->name = strdup(destDirList->name);
				dirList->next = destDirList->next;
			}
		}else{
			destPrev->next = dirList;
			dirList->next = destDirList->next;
			dirList->name = strdup(destDirList->name);
		}
	}else{
		destDirList->content = strdup(dirList->content);
	}
	/*printf("Rename returned\n");*/
	return 0;
}

int storeDir(struct filesStruct *parent,struct filesStruct *dirList, int level, int fd,int offset){

	char dir[10],dirStruct[10000];
	int written = 0;
	if(level == 1)
		sprintf(dir,"%s","#ROOT#");
	else
		sprintf(dir,"%s%s%s","#dir",parent->name,"#");
	written = pwrite(fd,dir,strlen(dir),offset);
	offset = written + offset;
	/*sprintf(dirStruct,"%s%s",dirStruct,dir);*/
	while(dirList != NULL){
		if(dirList->isfile == 0){
			sprintf(dirStruct,"%s%s%s%d%s%d%s%s%s","#node#",dirList->name,"|",dirList->isfile,"|",(dirList->subdir != NULL)?1:0,"|","NONE","#endnode#");
			written = pwrite(fd,dirStruct,strlen(dirStruct),offset);
			offset = written + offset;
		}

		if(dirList->subdir != NULL){
			offset = storeDir(dirList,dirList->subdir,level +1,fd,offset);
		}else if (dirList->isfile){
			/*printf("\tWriting...%s\n",dirList->name);*/
			sprintf(dirStruct,"%s%s%s%d%s%d%s","#node#",dirList->name,"|",dirList->isfile,"|",(dirList->subdir != NULL)?1:0,"|");
			written = pwrite(fd,dirStruct,strlen(dirStruct),offset);
			offset = written + offset;
			written = pwrite(fd,dirList->content != NULL ?dirList->content:"NONE",dirList->content != NULL ?strlen(dirList->content):strlen("NONE"),offset);
			offset = written + offset;
			sprintf(dirStruct,"%s","#endnode#");
			written = pwrite(fd,dirStruct,strlen(dirStruct),offset);
			offset = written + offset;
			/*printf("\tWrote...%s\n",dirList->name);*/
		}
		dirList = dirList->next;
	}
	if(level == 1)
		sprintf(dir,"%s","#ENDROOT#");
	else
		sprintf(dir,"%s%s%s","#enddir",parent->name,"#");
	written = pwrite(fd,dir,strlen(dir),offset);
	offset = written + offset;
	return offset;
}

void freeFiles(struct filesStruct *dirList){
	/*struct filesStruct *prev = NULL;*/
	/*printf("Total memory in freefiles write %d\n",totalMemory);*/
	int dirSize = 0;
	while(dirList != NULL){
		if(dirList->subdir != NULL)
			freeFiles(dirList->subdir);

		if(dirList->isfile == 1){
			/*printf("dirList->name\t%s\n",dirList->name);*/
			int length=0;
			if(dirList->content != NULL)
				length = strlen(dirList->content);
			else
				length = 0;
			dirSize = strlen(dirList->name) + length +sizeof(struct filesStruct);
		}
		else
			dirSize = strlen(dirList->name) + sizeof(struct filesStruct);

		totalMemory = totalMemory + dirSize;
		free(dirList->name);
		if(dirList->isfile == 1){
			if(dirList->content != NULL)
				free(dirList->content);
		}
		free(dirList);
		dirList = dirList->next;
	}
	/*printf("Total memory in after freefiles %d\n",totalMemory);*/
}

void ram_destroy (void *destroy){
	/*printf("destroy was called\n");
	printf("\twrite= %d\ttotal=%d\n",writeMemory,totalMemory);*/
	if(saveFile == 1){
		struct filesStruct *temp = ramfiles->head;
		int fd = open(mountPoint,O_WRONLY|O_CREAT,S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP);
		storeDir(temp, temp,1,fd,0);
		close(fd);
	}
	struct filesStruct *dirList = ramfiles->head;
	freeFiles(dirList);
}

int match(char *a, char *b)
{
   int position = 0;
   char *x, *y;

   x = a;
   y = b;

   while(*a)
   {
      while(*x==*y)
      {
         x++;
         y++;
         if(*x=='\0'||*y=='\0')
            break;
      }
      if(*y=='\0')
         break;

      a++;
      position++;
      x = a;
      y = b;
   }
   if(*a)
      return position;
   else
      return -1;
}

char *substring(char *string, int position, int length)
{
	int c;
   char *pointer;

   pointer = (char *)malloc(length+1);
   if (pointer == NULL)
   {
	   perror("malloc failed");
	   exit(EXIT_FAILURE);
   }
   for (c = 0 ; c < position -1 ; c++)
      string++;
   strncpy(pointer,string,length);
   return pointer;
}

int parseandload(struct filesStruct *parent,struct filesStruct *child,char *loaddir,int level){

	/*printf("Entering parser\n");*/
	int start = 0,end = 0;
	int hasChild = -1;
	char *nodeName,*localdir,*contentVal;
	localdir = strdup(loaddir);
	char dirstart[100] = "\0";
	char dirend[100] = "\0";
	char *filePtr,*nodeptr;

	start = match(localdir,"#node#");
	end = match(localdir,"#endnode#");
	/*printf("\nstart=%d\tend=%d\ttotal=%d\n",start,end,strlen(localdir));*/
	while(start != -1 || end != -1){
		int remLength;
		int subsize = end - (start + strlen("#node#"));
		filePtr = (substring(localdir,start + strlen("#node#")+1,subsize));
		nodeptr = strdup(filePtr);
		/*printf("after dup\n");
		printf("\n\tNode>>%s\n",nodeptr);*/
		struct filesStruct *addNode = (struct filesStruct *)malloc(sizeof(struct filesStruct));
		nodeName = strtok(nodeptr,"|");
		/*printf("\tNode name %s\n",nodeName);*/
		addNode->name = strdup(nodeName);
		/*printf("\tNode name %s\n",addNode->name);*/
		addNode->isfile = atoi(strtok(NULL,"|"));
		/*printf("\tisFile %d\n",addNode->isfile);*/
		hasChild = atoi(strtok(NULL,"|"));
		/*printf("\thasChild %d\n",hasChild);*/
		contentVal = strtok(NULL,"|");
		addNode->content = strdup(contentVal);
		/*printf("\tcontent %s\n",addNode->content);
		addNode->mode = 0755;*/

		addNode->subdir = NULL;
		addNode->next = NULL;
		if((totalMemory - strlen(nodeName) - sizeof(struct filesStruct *)) < 0){
			fprintf(stderr,"%s","NO ENOUGH SPACE");
				return -ENOSPC;
		}
		else
			totalMemory = totalMemory - strlen(nodeName) - sizeof(struct filesStruct *);
		struct filesStruct *dirList;
		if(level == 1){
			dirList = ramfiles->head;
			if(dirList == NULL){
				ramfiles->head = addNode;
				dirList = ramfiles->head;
			}else{
				while(dirList->next != NULL)
					dirList = dirList->next;
				dirList->next = addNode;
				dirList = addNode;
			}
		}else{
			/*printf("level = %d\n",level);*/
			dirList = parent->subdir;
			if(dirList == NULL){
				parent->subdir = addNode;
				dirList = parent->subdir;
				/*printf("Node added\n");*/
			}else{
				while(dirList->next != NULL)
					dirList = dirList->next;
				dirList->next = addNode;
				dirList = addNode;
			}
		}
		remLength = strlen(localdir)-(strlen("#endnode#") + subsize);
		localdir = (substring(localdir,end + strlen("#endnode#")+1,remLength));
		if(hasChild == 1){
			sprintf(dirstart,"%s%s%s","#dir",nodeName,"#");
			sprintf(dirend,"%s%s%s","#enddir",nodeName,"#");
			start = match(localdir,dirstart);
			end = match(localdir,dirend);
			int subsize = end - (start + strlen(dirstart));
			/*printf("\nstart=%d\tend=%d\ttotal=%d\n",start,end,strlen(localdir));*/
			filePtr = substring(localdir,start + strlen(dirstart)+1,subsize);

			parseandload(dirList,dirList->subdir,filePtr,level+1);

			remLength = strlen(localdir)-(strlen(dirend) + subsize);
			localdir = substring(localdir,end + strlen(dirend)+1,remLength);
		}
		start = match(localdir,"#node#");
		end = match(localdir,"#endnode#");
		free(nodeptr);
		/*printf("\nstart=%d\tend=%d\ttotal=%d\n",start,end,strlen(localdir));*/
	}
	free(localdir);
	return 0;
}

static struct fuse_operations xmp_oper = {
	.getattr	= xmp_getattr,
	.readdir	= xmp_readdir,
	.mkdir		= xmp_mkdir,
	.unlink		= xmp_unlink,
	.rmdir		= xmp_rmdir,
	.open		= xmp_open,
	.read		= xmp_read,
	.write		= xmp_write,
	.create		= ram_create,
	.opendir 	= ram_opendir,
	.destroy	= ram_destroy,
	.rename		= ram_rename,
	.truncate	= ram_truncate,
};

int main(int argc, char *argv[]){
	char *filebuf ;
	struct fileSystem *temp = (struct fileSystem *)malloc(sizeof(struct fileSystem));
	temp->head = NULL;
	char tempChar='/';
	temp->filePath = &tempChar;
	ramfiles = temp;
	int argCount = 0;

	/* without debug*/
	if(debug == 0){
		if(argc == 4 ){
			saveFile = 1;
			mountPoint = argv[3];
		}else if (argc == 3){
			saveFile = 0;
		}else {
			perror("Argument mismatch");
			exit(-1);
		}
		totalMemory = atoi(argv[2]);
		argCount = 2;
	}
	/* with debug*/
	if(debug == 1){
		if(argc > 4 ){
			saveFile = 1;
			mountPoint = argv[4];
		}else{
			saveFile = 0;
		}
		totalMemory = atoi(argv[3]);
		argCount = 3;
	}


	totalMemory = totalMemory * 1000 * 1000;
	/*printf("\tMemorysize=%d\n",totalMemory);*/

	if(saveFile == 1){
		FILE *fp;
		fp=fopen(mountPoint, "r");

		if (fp != NULL) {
			if (fseek(fp, 0L, SEEK_END) == 0) {
				long bufsize = ftell(fp);
				if (bufsize == -1) { /*printf("Error on buffsize\n");*/}
				filebuf = malloc(sizeof(char) * (bufsize + 1));
				if (fseek(fp, 0L, SEEK_SET) != 0) { /*printf("Error on seek\n"); */}
				size_t newLen = fread(filebuf, sizeof(char), bufsize, fp);
				if (newLen == 0) {
					fputs("Error reading file", stderr);
				} else {
					filebuf[++newLen] = '\0';
				}
			}
			fclose(fp);

			int start,end;
			char *filePtr;
			start = match(filebuf,"#ROOT#");
			end = match(filebuf,"#ENDROOT#");

			int subsize = end - (start + strlen("#ROOT#"));
			/*printf("\nstart=%d\tend=%d\tsubsize=%d\n",start,end,subsize);*/
			filePtr = strdup(substring(filebuf,start + strlen("#ROOT#")+1,subsize));
			struct filesStruct *temp = ramfiles->head;
			parseandload(temp,temp,filePtr,1);

			free(filePtr);
			/*printf("Parser returned\n");*/
		}
	}

	return fuse_main(argCount, argv, &xmp_oper, NULL);
}
