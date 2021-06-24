#ifndef __DLMALLOC_CONFIG_H_
#define __DLMALLOC_CONFIG_H_

/*
  This header is for ANSI C/C++ only.  You can set any of
  the following #defines before including:

  * If USE_DL_PREFIX is defined, it is assumed that malloc.c 
    was also compiled with this option, so all routines
    have names starting with "dl".

  * If HAVE_USR_INCLUDE_MALLOC_H is defined, it is assumed that this
    file will be #included AFTER <malloc.h>. This is needed only if
    your system defines a struct mallinfo that is incompatible with the
    standard one declared here.  Otherwise, you can include this file
    INSTEAD of your system system <malloc.h>.  At least on ANSI, all
    declarations should be compatible with system versions

  * If MSPACES is defined, declarations for mspace versions are included.
*/

#define USE_DL_PREFIX
#define NO_MALLINFO 1

#endif /* __DLMALLOC_CONFIG_H_ */
