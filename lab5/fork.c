/********************************************************************
Copyright 2010-2017 K.C. Wang, <kwang@eecs.wsu.edu>
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
********************************************************************/

int body(), goUmode();

/*************** kfork(filename)***************************
kfork() a new proc p with filename as its UMODE image.
Same as kfork() before EXCEPT:
1. Each proc has a level-1 pgtable at 6MB, 6MB+16KB, , etc. by pid
2. The LOW 258 entries of pgtable ID map 258 MB VA to 258 MB PA  
3. Each proc's UMODE image size = 1MB at 8MB, 9MB,... by pid=1,2,3,..
4. load(filenmae, p); load filenmae (/bin/u1 or /bin/u2) to p's UMODE address
5. set p's Kmode stack for it to 
           resume to goUmode
   which causes p to return to Umode to execute filename
***********************************************************/

PROC *kfork(char *filename)
{
  int i, upa, usp; 
  int pentry, *ptable;

  PROC *p = dequeue(&freeList);
  if (p==0){
    kprintf("kfork failed\n");
    return (PROC *)0;
  }

  printf("kfork %s\n", filename);
  
  p->ppid = running->pid;
  p->parent = running;
  p->status = READY;
  p->priority = 1;

  // build p's pgtable 
  p->pgdir = (int *)(0x600000 + (p->pid - 1)*0x4000);
  ptable = p->pgdir;
  // initialize pgtable
  for (i=0; i<4096; i++)
    ptable[i] = 0;

  pentry = 0x412;
  for (i=0; i<258; i++){
    ptable[i] = pentry;
    pentry += 0x100000;
  }

  // UMODE VA mapping: Assume each proc has a 1MB Umode space at 8MB+(pid-1)*1MB
  // ptable entry flag=|AP=11|0|dom1|1|CB10|=110|0001|1|1110|=0xC3E or 0xC32    
  //ptable[2048] = 0x800000 + (p->pid - 1)*0x100000|0xC3E; // CB=11
  ptable[2048] = 0x800000 + (p->pid - 1)*0x100000|0xC32;   // CB=00
  ptable[2049] = 0x1000000 + (p->pid - 1)*0x100000|0xC32; // mapping to 2MB
  // entries 2049 to 4095 all 0 for INVALID

  p->cpsr = (int *)0x10;    // saved cpsr = previous mode was Umode
  
  // set kstack to resume to goUmode, then to Umode image at VA=0
  for (i=1; i<29; i++)  // all 28 cells = 0
    p->kstack[SSIZE-i] = 0;

  p->kstack[SSIZE-15] = (int)goUmode;  // in dec reg=address ORDER !!!
  p->ksp = &(p->kstack[SSIZE-28]);

  // kstack must contain a syscall frame FOLLOWed by a resume frame
  // ulr = VA(0)
  // -|--- syscall frame: stmfd sp!,{r0-r12,lr} -----
  // ulrc u12 u11 u10 u9 u8 u7 u6 u5 u4 u3 u2 u1 u0
  //-------------------------------------------------
  //  1   2   3   4   5  6  7  8  9  10 11 12 13 14

  // klr=goUmode
  // -|-- tswitch frame: stmfd sp!,{r0-r12,lr} ---  ksp
  // klr r12 r11 r10 r9 r8 r7 r6 r5 r4 r3 r2 r1 r0
  // ----------------------------------------------------
  // 15  16  17  18  19 20 21 22 23 24 25 26 27 28
  //                                            
  // to go Umode, must set new PROC's Umode cpsr to IF=00 umode=b'10000'=0x10

  // must load filename to Umode image area at 7MB+(pid-1)*1MB
  //addr = (char *)(0x700000 + (p->pid)*0x100000);
  
  load(filename, p); // p->PROC containing pid, pgdir, etc
 
  // must fix Umode ustack for it to goUmode: how did the PROC come to Kmode?
  // by swi # from VA=0 in Umode => at that time all CPU regs are 0
  // we are in Kmode, p's ustack is at its Uimage (8mb+(pid-1)*1mb) high end
  // from PROC's point of view, it's a VA at 1MB (from its VA=0)
  // but we in Kmode must access p's Ustack directly

  /***** this sets each proc's ustack differently, thinking each in 8MB+
  ustacktop = (int *)(0x800000+(p->pid)*0x100000 + 0x100000);
  TRY to set it to OFFSET 1MB in its section; regardless of pid
  **********************************************************************/

  upa = p->pgdir[2048] & 0xFFFF0000; // PA of Umode image

  usp = upa + 0x100000 - 128;

  strcpy((char*)usp, "My name is Drayer Sivertsen\n");

  // p->usp = (int *)(0x80100000);
  p->usp = (int*)VA(0x100000 - 128); // (int *)VA(0x100000); <- previously was




  // p->kstack[SSIZE-1] = (int)0x80000000;
  p->kstack[SSIZE-1] = VA(0); // return uLR = VA(0)
  p->kstack[SSIZE - 14] = p->usp;

  // |---------------------- goUmode -----------------
  // |ulr u12 u11 u10 u9 u8 u7 u6 u5 u4 u3 u2 u2 u0
  //--------------------------------------------------
  //   1  2   3   4   5  6  7  8  9  10 11 12 13 14 

  enqueue(&readyQueue, p);

  kprintf("proc %d kforked a child %d: ", running->pid, p->pid); 
  printQ(readyQueue);

  return p;
}
