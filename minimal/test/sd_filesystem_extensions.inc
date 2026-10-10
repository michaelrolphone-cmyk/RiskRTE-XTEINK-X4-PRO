/* Real FatFs/volume implementation, native-card fixture transport. */
static void filesystem_extensions(void){
 const risc_driver_v2* d=t5_driver_get(2);const risc_storage_volume_api_v1_fs* fs=risc_storage_volume_fs(d->capability);assert(fs);
 assert(mkdir_path(NULL,"/books"));
 uint32_t a=file_open(NULL,"/books/a.txt",RISC_STORAGE_OPEN_WRITE|RISC_STORAGE_OPEN_CREATE);assert(a);assert(file_write(NULL,a,"alpha",5)==5);assert(file_close(NULL,a,true));
 uint32_t b=file_open(NULL,"/books/b.txt",RISC_STORAGE_OPEN_WRITE|RISC_STORAGE_OPEN_CREATE);assert(b);assert(file_write(NULL,b,"bravo",5)==5);assert(file_close(NULL,b,true));
 risc_storage_metadata_v1 m={.struct_size=sizeof(m)};assert(fs->metadata(NULL,"/books/a.txt",&m)==0&&m.size==5&&!(m.flags&RISC_STORAGE_DIRECTORY));assert(fs->metadata(NULL,"/missing",&m)==RISC_STORAGE_FS_NOT_FOUND);
 uint32_t dir=dir_open(NULL,"/books");assert(dir);risc_storage_dirent_v1 first,next;assert(dir_next(NULL,dir,&first));uint64_t position;assert(fs->dir_tell(NULL,dir,&position));assert(position&&position%32==0);assert(dir_next(NULL,dir,&next));assert(dir_close_checked(NULL,dir));
 dir=dir_open(NULL,"/books");assert(fs->dir_seek(NULL,dir,position));risc_storage_dirent_v1 resumed;assert(dir_next(NULL,dir,&resumed));assert(!strcmp(next.name,resumed.name));assert(dir_close_checked(NULL,dir));assert(!fs->dir_tell(NULL,dir,&position));
 b=file_open(NULL,"/books/b.txt",RISC_STORAGE_OPEN_WRITE);assert(fs->file_truncate(NULL,b,2));assert(!fs->file_truncate(NULL,b,6));assert(file_close(NULL,b,true));assert(fs->metadata(NULL,"/books/b.txt",&m)==0&&m.size==2);
 assert(!fs->replace_file(NULL,"/books/a.txt","/books/A.TXT"));
 assert(!fs->replace_file(NULL,"/books/a.txt","/books/a.txt.risc-replace"));
 assert(fs->replace_file(NULL,"/books/b.txt","/books/a.txt"));assert(fs->metadata(NULL,"/books/a.txt",&m)==0&&m.size==2);
 /* Simulate a reset after staging the original but before installing its replacement. */
 assert(rename_path(NULL,"/books/a.txt","/books/a.txt.risc-replace"));assert(fs->recover_replace(NULL,"/books/a.txt"));assert(fs->metadata(NULL,"/books/a.txt",&m)==0&&m.size==2);
 owner=false;assert(fs->metadata(NULL,"/books/a.txt",&m)==RISC_STORAGE_FS_IO);owner=true;
 assert(!has_handles());verify_cleanup();puts("Shared filesystem directory seek/tell, metadata, truncate, replacement and reset recovery PASS");
}
