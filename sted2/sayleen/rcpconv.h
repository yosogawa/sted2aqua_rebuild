/*
  RCP converter

  Copyright 1999 by Daisuke Nagano <breeze.nagano@nifty.ne.jp>
  May.28.1999
  Oct.16.2002


  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
*/

#ifndef _RCPCONV_H_
#define _RCPCONV_H_

#include <stddef.h>

extern int rcpconv( uint8_t *,  /* Pointer to RCP data */
		     size_t,           /* Length (bytes) of RCP data */
		     uint8_t **, /* Destination of conversion */
		     const char * /* Copyright notice */
		     );

extern int rcpconv_with_notice(
		     uint8_t *,  /* Pointer to RCP data */
		     size_t,           /* Length (bytes) of RCP data */
		     uint8_t **, /* Destination of conversion */
		     const char *,  /* Copyright notice */
		     const char *,  /* Converter's command name */
		     const char *,  /* Filename of original RCP file */
		     const char *   /* Timestamp of original RCP file */
		     );

#endif /* _RCPCONV_H_ */
