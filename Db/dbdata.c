/*
* $Id:  $
* $Version: $
*
* Copyright (c) Tanel Tammet 2004,2005,2006,2007,2008,2009
*
* Contact: tanel.tammet@gmail.com                 
*
* This file is part of wgandalf
*
* Wgandalf is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
* 
* Wgandalf is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
* 
* You should have received a copy of the GNU General Public License
* along with Wgandalf.  If not, see <http://www.gnu.org/licenses/>.
*
*/

 /** @file dbdata.c
 *  Procedures for handling actual data: records, strings, integers etc
 *
 */

/* ====== Includes =============== */


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

#include "../config.h"
#include "dballoc.h"
#include "dbdata.h"

/* ====== Private headers and defs ======== */

/* ======= Private protos ================ */


/* ====== Functions ============== */


/* ------------ full record handling ---------------- */

void* wg_create_record(void* db, int length) {
  gint offset;
  gint i;
  
#ifdef CHECK
  if (!dbcheck(db)) {
    show_data_error_nr(db,"wrong database pointer given to wg_create_record with length ",length); 
    return NULL;
  }  
#endif  
  offset=alloc_gints(db,
                     &(((db_memsegment_header*)db)->datarec_area_header),
                    length+RECORD_HEADER_GINTS);
  if (!offset) {
    show_data_error_nr(db,"cannot create a record of size ",length); 
    return NULL;
  }      
  for(i=RECORD_HEADER_GINTS;i<length+RECORD_HEADER_GINTS;i++) {
    dbstore(db,offset+RECORD_HEADER_GINTS,0);
  }     
  return offsettoptr(db,offset);
}  


void* wg_get_first_record(void* db) {
  db_subarea_header* arrayadr;
  gint firstoffset;
  void* res;

#ifdef CHECK
  if (!dbcheck(db)) {
    show_data_error(db,"wrong database pointer given to wg_get_first_record"); 
    return NULL;
  }  
#endif 
  arrayadr=&((((db_memsegment_header*)db)->datarec_area_header).subarea_array[0]);
  firstoffset=((arrayadr[0]).alignedoffset); // do NOT skip initial "used" marker
  printf("arrayadr %x firstoffset %d \n",(uint)arrayadr,firstoffset);
  res=wg_get_next_record(db,offsettoptr(db,firstoffset));
  return res;  
}

void* wg_get_next_record(void* db, void* record) {
  gint curoffset;
  db_subarea_header* arrayadr;
  gint last_subarea_index;
  gint head;
  gint i;
  gint found;
  gint dv;
  gint dvsize;
  gint dvend;
  gint subareastart;
  gint subareaend;

  curoffset=ptrtooffset(db,record);
  printf("curroffset %d record %x\n",curoffset,(uint)record);
#ifdef CHECK
  if (!dbcheck(db)) {
    show_data_error(db,"wrong database pointer given to wg_get_first_record"); 
    return NULL;
  }  
  if (isfreeobject(curoffset)) {
    show_data_error(db,"wrong record pointer (free) given to wg_get_next_record"); 
    return NULL;
  }  
#endif   
  
  arrayadr=&((((db_memsegment_header*)db)->datarec_area_header).subarea_array[0]);
  last_subarea_index=(((db_memsegment_header*)db)->datarec_area_header).last_subarea_index;
  // locate subarea for this offset 
  found=0;
  for(i=0;(i<=last_subarea_index)&&(i<SUBAREA_ARRAY_SIZE);i++) {
    subareastart=((arrayadr[i]).alignedoffset);
    subareaend=((arrayadr[i]).offset)+((arrayadr[i]).size)-MIN_VARLENOBJ_SIZE;
    if (curoffset>=subareastart && curoffset<subareaend) {
      found=1;
      break;          
    }          
  }  
  if (!found) {
    show_data_error(db,"wrong record pointer (out of area) given to wg_get_next_record"); 
    return NULL;
  } 
  // increase offset to next memory block
  head=dbfetch(db,curoffset);
  curoffset=curoffset+getusedobjectsize(head);    
  // check if curoffset in bounds, free and not in dv 
  dv=((((db_memsegment_header*)db)->datarec_area_header).freebuckets)[DVBUCKET];
  dvsize=((((db_memsegment_header*)db)->datarec_area_header).freebuckets)[DVSIZEBUCKET];
  dvend=dv+dvsize;
  while(1) {       
    // designated victim case: not really a used object
    if (curoffset>=dv && curoffset<dvend) {
      printf("curoffset %d is in dv %d with size %d\n",curoffset,dv,dvsize); 
      curoffset=dvend;      
    }               
    // check that not out of bounds
    if (curoffset>=subareaend) {
      // take next subarea, while possible
      i++;
      if (i>last_subarea_index || i>=SUBAREA_ARRAY_SIZE) {
        printf("next used object not found: i %d curoffset %d \n",i,curoffset); 
        return (void*)NULL; 
      }        
      printf("taking next subarea i %d\n",i);
      subareastart=((arrayadr[i]).alignedoffset)+MIN_VARLENOBJ_SIZE;
      subareaend=((arrayadr[i]).offset)+((arrayadr[i]).size)-MIN_VARLENOBJ_SIZE;
      curoffset=subareastart;     
    }  
    head=dbfetch(db,curoffset);
    // check if a used object
    if (!isfreeobject(head) && (curoffset<dv || curoffset>=dvend)) {    
      printf("curoffset %d head gint %d is used\n",curoffset,head);
      return (void*)curoffset; //offsettoptr(db,curoffset);
    }  
    // current object is free, jump to next object    
    printf("curoffset %d head gint %d is free\n",curoffset,head);
    curoffset=curoffset+getfreeobjectsize(head);    
  }  
}  

/* ------------ field handling ---------------- */


int wg_set_int_field(void* db, void* record, int fieldnr, int data) {
  gint offset;
  
#ifdef CHECK
  recordcheck(db,record,fieldnr,"wg_set_int_field");
#endif  
  if (fits_smallint(data)) {
    *(((gint*)record)+RECORD_HEADER_GINTS+fieldnr)=encode_smallint(data);
    //dbstore(db,ptrtoffset(record)+RECORD_HEADER_GINTS+fieldnr,encode_smallint(data));
  } else {
    offset=alloc_word(db);
    if (!offset) {
      show_data_error_nr(db,"cannot store an integer in wg_set_int_field: ",data); 
      return -3;
    }    
    dbstore(db,offset,data);
    *(((gint*)record)+RECORD_HEADER_GINTS+fieldnr)=encode_fullint_offset(offset);
    //dbstore(db,ptrtoffset(record)+RECORD_HEADER_GINTS+fieldnr,encode_fullint_offset(offset));
  }
  return 0;     
}  

int wg_set_double_field(void* db, void* record, int fieldnr, double data) {
  gint offset;
  
#ifdef CHECK
  recordcheck(db,record,fieldnr,"wg_set_double_field");
#endif  
  if (0) {
    // possible future case for tiny floats
  } else {
    offset=alloc_doubleword(db);
    if (!offset) {
      show_data_error_double(db,"cannot store a double in wg_set_double_field",data); 
      return -3;
    }    
    dbstore(db,offset,data);
    *(((gint*)record)+RECORD_HEADER_GINTS+fieldnr)=encode_fulldouble_offset(offset);
    //dbstore(db,ptrtoffset(record)+RECORD_HEADER_GINTS+fieldnr,encode_fulldouble_offset(offset));
  }
  return 0;     
} 

int wg_set_str_field(void* db, void* record, int fieldnr, char* str) {
  gint offset;
  gint len;
  gint data;
  gint i;
  gint shft;
  char* dptr;
  char* sptr;
  char* dendptr;
  
#ifdef CHECK
  recordcheck(db,record,fieldnr,"wg_set_str_field");
#endif  
  len=(gint)(strlen(str));
  if (len<sizeof(gint)) {
    // tiny string, store directly
    data=TINYSTRBITS; // first zero the field and set last byte to mask
    // loop over bytes, storing them to data gint by shifting
    for(i=0,shft=(gint)(sizeof(gint)-1)*8; i<len; i++,shft=shft-8) {
      data = data |  ((*(str+i))<<shft); // shift byte to correct position
    }      
    *(((gint*)record)+RECORD_HEADER_GINTS+fieldnr)=data;
    //dbstore(db,ptrtoffset(record)+RECORD_HEADER_GINTS+fieldnr,data);
  } else if (len<SHORTSTR_SIZE) {
    // short string, store in a fixlen area
    offset=alloc_shortstr(db);
    if (!offset) {
      show_data_error_str(db,"cannot store a string in wg_set_str_field",str); 
      return -3;
    }    
    // loop over bytes, storing them starting from offset
    dptr=offsettoptr(db,offset);
    dendptr=dptr+SHORTSTR_SIZE;
    for(sptr=str; (*dptr=*sptr)!=0; sptr++, dptr++) {}; // copy string
    for(dptr++; dptr<dendptr; dptr++) { *dptr=0; }; // zero the rest 
    // store offset to field    
    *(((gint*)record)+RECORD_HEADER_GINTS+fieldnr)=encode_shortstr_offset(offset);
    //dbstore(db,ptrtoffset(record)+RECORD_HEADER_GINTS+fieldnr,encode_shortstr_offset(offset));    
  } else {
    // long string: find hash, check if exists and point or allocate new
    // currently not implemented
    show_data_error_nr(db,"cannot store a string in wg_set_str_field for str length: ",len);
  }
  return 0;     
} 


/* ------------ errors ---------------- */


gint show_data_error(void* db, char* errmsg) {
  printf("wg data handling error: %s\n",errmsg);
  return -1;
}

gint show_data_error_nr(void* db, char* errmsg, gint nr) {
  printf("wg data handling error: %s %d\n",errmsg,nr);
  return -1;
}

gint show_data_error_double(void* db, char* errmsg, double nr) {
  printf("wg data handling error: %s %f\n",errmsg,nr);
  return -1;
}

gint show_data_error_str(void* db, char* errmsg, char* str) {
  printf("wg data handling error: %s %s\n",errmsg,str);
  return -1;
}

