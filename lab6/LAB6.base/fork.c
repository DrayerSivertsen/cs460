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

int goUmode();
char *istring = "init start";

PROC *kfork(char *filename)
{
  int i;
  //  char line[8], *cp;
  int *ptable, pentry;  

  PROC *p = dequeue(&freeList);
  if (p==0){
    kprintf("kfork failed\n");
    return (PROC *)0;
  }
  p->ppid = running->pid;
  p->parent = running;
  p->status = READY;
  p->priority = 1;
  
  // 1-level paging by 1MB sections
  // build level-1 pgtable for p at 6MB + (pid-1)*16KB
  p->pgdir = (int *)(0x600000 + (p->pid-1)*0x4000); // must be on 16KB boundary 

  ptable = p->pgdir;
  for (i=0; i<4096; i++)  // clear ptable entries
    ptable[i] = 0;
  
  // Kmode: ptable[0-257] ID map to 258 PA, Kmode RW but no Umode RW
  pentry = 0x412;   // 0x412 = |AP|0|DOMA|1|CB10|=|01|0|0000|1|0010| 
  for (i=0; i<258; i++){
    ptable[i] = pentry;
    pentry += 0x100000;
  }
  
  // Umode: ptable[2048] map to 1MB PA of proc at 8MB, 9MB, etc by pid
  //|     addr     | |       |AP|0|DOM1|1|CB|10|
  // 0xC12 = 1100 0001 0010 =|11|0|0001|1|00|10|       // AP=11 for Umode RW
  ptable[2048]=(0x800000 + (p->pid-1)*0x100000)|0xC32; // entry 2048 | 0xC32  
  ptable[2049] = 0x1000000 + (p->pid - 1)*0x100000|0xC32; // mapping to 2MB

  // set kstack to resume to goUmode
  for (i=1; i<29; i++)  // all 28 cells = 0
    p->kstack[SSIZE-i] = 0;

  // kstack must contain a syscall frame FOLLOWed by a tswitch frame
  //  ---------------- syscall FRAME -------------------
  //  ulr u12 u11 u10 u9 u8 u7 u6 u5  u4  u3  u2  u1  u0      // ulr=VA(0),u0=r
  //  --------------------------------------------------
  //  -1  -2  -3  -4 -5  -6 -7 -8 -9 -10 -11 -12 -13 -14
  //  
   // -|-------------- tswitch frame -------------------------
  //  klr k12 k11 k10  k9  k8  k7  k6  k5  k4  k3  k2  k1  k0 // klr=goUmode
  //-----------------------------------------------------------
  //  -15 -16 -17 -18 -19 -20 -21 -22 -23 -24 -25 -26 -27 -28

  p->kstack[SSIZE-15] = (int)goUmode;  // in dec reg=address ORDER !!!
  p->ksp = &(p->kstack[SSIZE-28]);

  // must load filename to Umode image area at 8MB+(pid-1)*1MB
  if( load(filename, p) < 0){
    printf("loading %s failed\n", filename);
    return -1;
  }

  // ustack at high end of Umode image: put a string there  
  char *s = (p->pgdir[2048] & 0xFFFF000) + 0x100000 - 128;
  strcpy(s, istring);
  printf("s=%x string=%s\n", s, s);
  
  p->usp = (int *)VA(0x100000 - 128);
  p->upc = (int *)VA(0);         
  p->cpsr = (int *)0x10;
  
  p->kstack[SSIZE-14] = (int)VA(0x100000 - 128);  // saved uR0 to Umode
  p->kstack[SSIZE-1] = (int)0x80000000;
  
  enqueue(&readyQueue, p);

  kprintf("proc %d kforked a child %d\n", running->pid, p->pid); 
  printQ(readyQueue);
 
  return p;
}

int fork()
{
  int i;
  char *PA, *CA;
  int *ptable, pentry;  
  
  // 1. Get a proc from freeList
  PROC *p = dequeue(&freeList);
  if (p==0) { printf("fork failed\n"); return -1; }
  // 2. write code to build pgdir and pgtable for p as in kfork()
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
  // 3. fork code as in Chapter 7.7.6

  p->ppid = running->pid;
  p->parent = running;
  p->status = READY;
  p->priority = 1;
  PA = (char *)(running->pgdir[2048] & 0xFFFF0000); // parent Umode PA
  CA = (char *)(p->pgdir[2048] & 0xFFFF0000); // child Umode PA
  printf("copy %xto %x\n", PA, CA);
  memcpy(CA, PA, 0x100000); // copy 1MB of Umode image

  PA = (char *)(running->pgdir[2049] & 0xFFFF0000); // parent Umode PA
  CA = (char *)(p->pgdir[2049] & 0xFFFF0000); // child Umode PA
  printf("copy %xto %x\n", PA, CA);
  memcpy(CA, PA, 0x100000); // copy 1MB of Umode image

  printf("copy kernel mode stack\n");
  for (i = 1; i <= 14; i++) // copy bottom 14 entries of kstack
  {
    p->kstack[SSIZE - i] = running->kstack[SSIZE - i];
  }

  p->kstack[SSIZE - 14] = 0; // child return pid = 0
  p->kstack[SSIZE - 15] = (int)goUmode; // shild resumes to goUmode
  p->ksp = &(p->kstack[SSIZE - 28]); // child saved ksp
  p->usp = running->usp; // same usp as parent
  p->upc = (int *)VA(p->pid);  
  printf("FIX UP child resumes PC to %x\n", (int)running->upc - 36);
  p->cpsr = running->cpsr; // same cpsr as parent
  enqueue(&readyQueue, p);
  printf("KERNEL: proc %dforked a child %d: ", running->pid, p->pid);
  printQ(readyQueue);

  return p->pid;
}
