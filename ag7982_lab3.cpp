#include <stdio.h>
#include <unistd.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <fstream>
#include <ctype.h>
#include <sstream>
#include <deque>
#include <queue>
#include <vector>
using namespace std;

//////////////////////////////  GLOBAL VARIABLES //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
ifstream myfile;
const int page_table_size = 64;
const int tau=49;
int max_frame_table_size;
bool debug = false;
unsigned long long int context_switch=0;
unsigned long long int process_exit=0;
unsigned long long int map_count=0;
unsigned long long int unmap_count=0;
unsigned long long int ins_count=0;
unsigned long long int fins_count=0;
unsigned long long int outs_count=0;
unsigned long long int fouts_count=0;
unsigned long long int zeros_count=0;
unsigned long long int segv_count=0;
unsigned long long int segp_count=0;
unsigned long long int total_cost = 0;
long int instruction_count=0;
bool o_flag = false;
bool f_flag = false;
bool p_flag = false;
bool s_flag = false;


vector <int> random_list;
int num_random;
int offset=0;

int get_random(int burst)
{
      int temp = random_list[offset++] % burst;
      if (offset == num_random)
      {
	offset = 0;	
      }
      return temp;
}

//////////////////////////// PROCESS CLASS /////////////////////////////////////////////////////////////////////////////////////////
class Process
{
    public:
    struct vma
    {
        int start;
        int end;
        int filemap;
        int writeproc;
    };
    struct page_table_entry
    {
        //PRESENT/VALID, REFERENCED, MODIFIED, WRITE_PROTECT, and   PAGEDOUT (SWAPPED) bits
        public:
        unsigned int frame_number: 7;
        unsigned int referenced: 1;
        unsigned int modified: 1;
        unsigned int present: 1;
        unsigned int write_protect: 1;
        unsigned int paged_out: 1;
        unsigned int valid: 1;
        unsigned int file_mapped: 1;
        unsigned int vma_bits_set: 1;
        unsigned int file_out: 1;

    };
    struct stats
    {
        long long int maps=0;
        long long int unmaps=0;
        long long int ins=0;
        long long int outs=0;
        long long int fins=0;
        long long int fouts=0;
        long long int zeros=0;
        long long int segv=0;
        long long int segprot=0;
    };
    
    page_table_entry pte;
    stats s;
    int process_id;
    page_table_entry page_table[page_table_size];
    vector <vma> vma_list;
    page_table_entry initialise_pte_to_zero()
    {
        page_table_entry pte;
        pte.frame_number = 0;
        pte.modified = 0;
        pte.present = 0;
        pte.paged_out = 0;
        pte.referenced = 0;
        pte.write_protect = 0;
        pte.file_mapped = 0;
        pte.vma_bits_set = 0;
        pte.file_out = 0;
        return pte;
    }
    void initialise_page_table()
    {
        for (int i=0;i<page_table_size;i++)
        {
            page_table_entry pte = initialise_pte_to_zero();
            page_table[i] = pte;
        }
        return;
    }
    void create_vma(int start, int end, int write, int file)
    {
        vma v;
        v.start = start;
        v.end = end;
        v.filemap = file;
        v.writeproc = write;
        this->vma_list.push_back(v);
        return;
    }
};
vector <Process *> process_list;

//////////////////////////////////////////////////////FRAME TABLE /////////////////////////////////////////////////////////
struct frame_table_entry
    {
        public:

        unsigned int age = 0;
        Process::page_table_entry * page_table_entry;
        int frame_number;
        int last_used = 0;
        int vpage;
        int process_number = 0;
        bool mapped=false;
    };

frame_table_entry * frame_table;
deque <frame_table_entry *> free_list;
void initialise_frame_table()
{
    for (int i = 0; i<max_frame_table_size; i++)
    {
        frame_table_entry fte;
        fte.frame_number = i;
        fte.mapped = false;
        fte.vpage = 0;
        //  cout<<"fte:"<<fte.frame_number<<endl;
        frame_table[i]=fte;

        // cout<<frame_table[i].frame_number<<": ft"<<endl;

    }
    return;
}

///////////////////////////////////////////// PAGER ////////////////////////////////////////////////////////////////////////////

class Pager
{
    public:
    virtual frame_table_entry * get_victim_frame()=0;
    virtual frame_table_entry * reset_age(frame_table_entry * f) =0;
};

////////// RANDOM //////////////////////
class Random:public Pager
{
    public:
    int head=0;
     frame_table_entry * reset_age(frame_table_entry * f)
    {
        // cout<<"lul"<<endl;
        // fflush(stdout);

        return f;
    }
    frame_table_entry * get_victim_frame()
    {
        
        head = get_random(max_frame_table_size) % max_frame_table_size;
        frame_table_entry * victim;
        victim = &frame_table[head];
        return victim;
    }
};

///////////// FIFO///////////////////////
class FIFO:public Pager
{
    public:
    int head = 0;
     frame_table_entry * reset_age(frame_table_entry * f)
    {
        return f;
    }
    frame_table_entry * get_victim_frame()
    {
        head = (head)%(max_frame_table_size);
        frame_table_entry * victim;
        victim = &(frame_table[head]);
        head++;
        
        return victim;
    }
};

////////////////////CLOCK///////////////////////////////
class Clock:public Pager
{
    public:
    int head = 0;
     frame_table_entry * reset_age(frame_table_entry * f)
    {
        return f;
    }
    frame_table_entry * get_victim_frame()
    {
        
        head = (head)%(max_frame_table_size);
        frame_table_entry * victim;
        victim = &(frame_table[head]);
        int temp = head;
        int count = 0;
        // cout<<"Check refernced bit: "<<victim->vpage<<" "<<victim->page_table_entry->referenced<<endl;
        while (victim->page_table_entry->referenced == 1)
        {
            head+=1;
            head = (head)%(max_frame_table_size);
            count+=1;
            victim->page_table_entry->referenced = 0;
            victim = &(frame_table[head]);
        }
        // cout<<"ASELECT: "<<temp<<" "<<count<<endl;
        head+=1;
        
        head = (head)%(max_frame_table_size);
        
        return victim;
    }
};
/////////////// NRU ////////////////////////////////////////
// first entry of all classes
class NRU: public Pager
{
    public:
    int head = 0;
    int prev_inst=0;
    frame_table_entry * get_victim_frame()
    {

        head = (head)%(max_frame_table_size);
        //CLASS ZERO
        for (int i=0;i<max_frame_table_size;i++)
        {
            if (frame_table[((head+i)%max_frame_table_size)].mapped && frame_table[((head+i)%max_frame_table_size)].page_table_entry->referenced == 0 && frame_table[((head+i)%max_frame_table_size)].page_table_entry->modified==0)
            {
                frame_table_entry * f = &(frame_table[((head+i)%max_frame_table_size)]);
                head=((head+i)+1)%max_frame_table_size;
                if (instruction_count - prev_inst > tau)
                {
                    prev_inst = instruction_count;
                    for (int j=0;j<max_frame_table_size;j++)
                    {
                            frame_table[j].page_table_entry->referenced = 0;
                    }
                }
                return f;
            }
        }
        //CLASS ONE
        for (int i=0;i<max_frame_table_size;i++)
        {
            if (frame_table[((head+i)%max_frame_table_size)].mapped && frame_table[((head+i)%max_frame_table_size)].page_table_entry->referenced == 0 && frame_table[((head+i)%max_frame_table_size)].page_table_entry->modified==1)
            {
                frame_table_entry * f = &(frame_table[((head+i)%max_frame_table_size)]);
                head=((head+i)+1)%max_frame_table_size;
                if (instruction_count - prev_inst > tau)
                {
                    prev_inst = instruction_count;
                    for (int j=0;j<max_frame_table_size;j++)
                    {
                            frame_table[j].page_table_entry->referenced = 0;
                    }
                }
                return f;
            }
        }
        /// CLASS TWO
        for (int i=0;i<max_frame_table_size;i++)
        {
            if (frame_table[((head+i)%max_frame_table_size)].mapped && frame_table[((head+i)%max_frame_table_size)].page_table_entry->referenced == 1 && frame_table[((head+i)%max_frame_table_size)].page_table_entry->modified==0)
            {
                frame_table_entry * f = &(frame_table[((head+i)%max_frame_table_size)]);
                head=((head+i)+1)%max_frame_table_size;
                if (instruction_count - prev_inst > tau)
                {
                    prev_inst = instruction_count;
                    for (int j=0;j<max_frame_table_size;j++)
                    {

                            frame_table[j].page_table_entry->referenced = 0;
                    }
                }
                return f;
            }
        }

        /// CLASS THREE
        for (int i=0;i<max_frame_table_size;i++)
        {
            if (frame_table[((head+i)%max_frame_table_size)].mapped && frame_table[((head+i)%max_frame_table_size)].page_table_entry->referenced == 1 && frame_table[((head+i)%max_frame_table_size)].page_table_entry->modified==1)
            {
               
                frame_table_entry * f = &(frame_table[((head+i)%max_frame_table_size)]);
                head=((head+i)+1)%max_frame_table_size;
                if (instruction_count - prev_inst > tau)
                {
                    prev_inst = instruction_count;
                    for (int j=0;j<max_frame_table_size;j++)
                    {
                            frame_table[j].page_table_entry->referenced = 0;
                    }
                }
                return f;
            }
        }
    }
     frame_table_entry * reset_age(frame_table_entry * f)
    {
        return f;
    }
};

/////////////// AGING ////////////////////////////////////////

class Aging: public Pager
{
    public:
    int head = 0;
    frame_table_entry * reset_age(frame_table_entry * f)
    {
        f->age = 0;
        return f;
    }
    frame_table_entry * get_victim_frame()
    {
        head = head%max_frame_table_size;
        for (int i=0;i<max_frame_table_size;i++)
        {
            frame_table_entry * ft = &frame_table[i];
            ft->age = ft->age >> 1;
            if (ft->page_table_entry->referenced == 1)
            {
                ft->age = (ft->age | 0x80000000);
            }
            ft->page_table_entry->referenced = 0;
        }
        int maxx = frame_table[head].age;
        int temp=head;
        for (int i=0;i<max_frame_table_size;i++)
        {
            frame_table_entry * ft = &frame_table[(head+i)%max_frame_table_size];
            // cout<<" current age: "<<head<<" "<<maxx<<" frame_age:"<<(head+i)%max_frame_table_size<<" "<<ft->age<<endl;
            if (ft->age < maxx)
            {
                temp= ft->frame_number;
                maxx = ft->age;
            }
        }
        frame_table_entry * victim = &frame_table[temp%max_frame_table_size];
        head = (temp+1)%max_frame_table_size;
        return victim;


        
    }
};

/////////////// WORKING SET ////////////////////////////////////////

class WS: public Pager
{
    public:
    int head = 0;
    
    frame_table_entry * get_victim_frame()
    {
        head = head%max_frame_table_size;
        int res = head;
        for (int i=0;i<max_frame_table_size;i++)
        {
            if (frame_table[(head+i)%max_frame_table_size].page_table_entry->referenced == 1)
            {
                frame_table[(head+i)%max_frame_table_size].last_used = instruction_count;
                frame_table[(head+i)%max_frame_table_size].page_table_entry->referenced = 0;
            }
            else if (frame_table[(head+i)%max_frame_table_size].page_table_entry->referenced == 0)
            {
                if (instruction_count - frame_table[(head+i)%max_frame_table_size].last_used > tau)
                {
                    res = (head+i)%max_frame_table_size;
                    break;
                }
                else
                {
                    if (frame_table[(head+i)%max_frame_table_size].last_used < frame_table[res].last_used )
                    {
                        res = (head+i)%max_frame_table_size;
                    }
                }
            }
        }
        head = (res+1)% max_frame_table_size;
        return &frame_table[res];
        
    }
     frame_table_entry * reset_age(frame_table_entry * f)
    {
        return f;
    }
};

///////////////////////////////////////////////////// GLOBAL FUNCTIONS ////////////////////////////////////////

void initialise_random_list(string rfile_path) //pushing random numbers into a list
{
    ifstream rfile;
    rfile.open(rfile_path);
    if (!rfile.is_open())
    {
        cout<<"Random File could not be opened"<<endl;
        return;
    }
    string line;
    getline(rfile, line);
    num_random=stoi(line);
    while (getline(rfile, line))
    {
        random_list.push_back(stoi(line));
    }
    return;
}

class Utility_Functions
{
    public:
        string get_line()
        {
            string line;
            getline(myfile, line);
            return line;
        }
        void print_process_page_table()
        {
            for (int i=0;i<process_list.size();i++)
            {
                cout<<"PT["<<i<<"]:";
                Process::page_table_entry * p;
                for (int j=0;j<page_table_size;j++)
                {
                    if (process_list[i]->page_table[j].present)
                    {
                        cout<<" "<<j<<":";
                        if (process_list[i]->page_table[j].referenced)
                        {
                            cout<<"R";
                        }
                        else
                        {
                            cout<<"-";
                        }

                        if (process_list[i]->page_table[j].modified)
                        {
                            cout<<"M";
                        }
                        else
                        {
                            cout<<"-";
                        }
                        if (process_list[i]->page_table[j].paged_out==1)
                        {
                            cout<<"S";
                        }
                        else
                        {
                            cout<<"-";
                        }
                    }
                    else
                    {
                        if (process_list[i]->page_table[j].paged_out == 1)
                        { cout<<" #";}
                        else
                        {
                            cout<<" *";
                        }
                    }
                }
                cout<<endl;
            }
        }
        void initialise_myfile(string file)
        {
            myfile.open(file);
            return;
        }

        void read_processes(int i)
        {
            string line = get_line();
            Process *cur = new Process();
            
            while (line[line.find_first_not_of(" ")] == '#')
            {
                line = get_line();
            }
            int vma_count = stoi(line);
            cur->process_id = i;
            cur->initialise_page_table();
            for (int j=0; j<vma_count;j++)
            {
                line = get_line();
                while (line[line.find_first_not_of(' ')] == '#')
                {
                    line = get_line();
                }
                char* c = strcpy(new char[line.length() + 1], line.c_str());
                int startpage, endpage, writeprotected, filemapped;
                sscanf(c, "%d %d %d %d", &startpage, &endpage, &writeprotected, &filemapped);
                cur->create_vma(startpage, endpage, writeprotected, filemapped);
                
                if (debug){cout<<"Got values:"<<startpage<<" "<<endpage<<" "<<writeprotected<<" "<<filemapped<<endl; }
            }
            process_list.push_back(cur);

            return;
        }
        void calculate_total_cost()
        {
            for (int i=0;i<process_list.size();i++)
            {
                Process * proc = process_list[i];
                printf("PROC[%d]: U=%lu M=%lu I=%lu O=%lu FI=%lu FO=%lu Z=%lu SV=%lu SP=%lu\n",proc->process_id,proc->s.unmaps, proc->s.maps, proc->s.ins, proc->s.outs,proc->s.fins,proc->s.fouts, proc->s.zeros,proc->s.segv, proc->s.segprot);
                map_count+=proc->s.maps;
                unmap_count+=proc->s.unmaps;
                ins_count+=proc->s.ins;
                fins_count+=proc->s.fins;
                outs_count+=proc->s.outs;
                fouts_count+=proc->s.fouts;
                zeros_count+=proc->s.zeros;
                segv_count+=proc->s.segv;
                segp_count+=proc->s.segprot;
            }

            
            total_cost+= (map_count*300);
            total_cost+=(unmap_count*400);
            total_cost+=(ins_count*3100);
            total_cost+=(fins_count*2800);
            total_cost+=(outs_count*2700);
            total_cost+=(fouts_count*2400);
            total_cost+=(zeros_count*140);
            total_cost+=(segv_count*340);
            total_cost+=(segp_count*420);
            
            total_cost+=(context_switch* 130);
            total_cost += (process_exit*1250);
            
        }
        void print_free_table()
        {
            int i = 0;
            for (deque<frame_table_entry *>::iterator it = free_list.begin();it!=free_list.end(); it++)
            {
                cout<<"Free Table entry "<<i<<" frame number: "<<(*it)->frame_number<<endl;
                i++;
            }
        }
        void print_frame_table()
        {
            cout<<"FT:";
            for (int i=0;i<max_frame_table_size;i++)
            {
                if (frame_table[i].mapped)
                
                   { cout<<" "<<frame_table[i].process_number<<":"<<frame_table[i].vpage;}
                else
                {
                    cout<<" *";
                }
            }
            cout<<endl;
            return;
        }
        void initialise_free_list()
        {
            for (int i=0;i<max_frame_table_size;i++)
            {
                free_list.push_back(&frame_table[i]);
            }

            if (debug) { print_free_table(); }
            return;
        }
        Pager * get_pager_object(string schedu)
        {
            Pager * sched;
            

            switch (schedu[0])
            {
                case ('f'):
                {
                    sched = new FIFO();
                    
                    break;
                }
                case ('c'):
                {
                    sched = new Clock();
                    break;
                }
                case ('r'):
                {
                    sched = new Random();
                    break;
                }
                
                case ('e'):
                {
                    sched = new NRU();
                    break;
                }
                case ('a'):
                {
                    sched = new Aging();
                    break;
                }
                case ('w'):
                {
                    sched = new WS();
                    break;
                }
                
            }
            return sched;
        }
};

//////////////////////////////////////// SIMULATION FUNCTIONS /////////////////////////////////////////////////////////////////////


frame_table_entry * get_frame(Pager * pager)
{
     
    frame_table_entry * fte;
    if (!free_list.empty())
    {
        
        fte = free_list.front();
        free_list.pop_front();
        
    }
    else
    {
    
        fte = pager->get_victim_frame();
       
    }
    return fte;
}

bool is_valid_vma(int vma_num, Process * p)
{
   vector <Process::vma>::iterator it = p->vma_list.begin();
   while (it!= p->vma_list.end())
   {
       if (((*it).start <= vma_num) && ((*it).end >= vma_num))
       {
           p->pte.vma_bits_set = 1;
           return true;
       }
       it++;
   }
   return false;
}
int get_file_mapped(int vma_num, Process * p)
{
    vector <Process::vma>::iterator it = p->vma_list.begin();
    while (it!= p->vma_list.end())
    {
        if (((*it).start <= vma_num) && ((*it).end >= vma_num))
        {
            return (*it).filemap;
        }
        it++;
    }
   
}
int get_write_protect(int vma_num, Process * p)
{
    vector <Process::vma>::iterator it = p->vma_list.begin();
    while (it!= p->vma_list.end())
    {
        if (((*it).start <= vma_num) && ((*it).end >= vma_num))
        {
            return (*it).writeproc;
        }
        it++;
    }
    
}

///////////////////////////////////////////// SIMULATION /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////// NRU ////////////////////////////////////////

void simulation(Pager * pager)
{
    string line;
    instruction_count = 0;
    while (getline(myfile,line))
        {
            while (line[0] == '#')
                {
                    getline(myfile, line);
                }
            
            if (!myfile.eof())
            {
                char instruction_type;
                int proc;
                Process *current;
                char* c_one = strcpy(new char[line.length() + 1], line.c_str());
                sscanf(c_one, "%c %d", &instruction_type, &proc);
                if (o_flag) cout<<instruction_count<<": ==> "<<instruction_type<<" "<<proc<<endl;
                instruction_count++;

                switch (instruction_type)
                {
                    case 'c':
                    {
                        context_switch+=1;
                        current = process_list[proc];
                        break;
                    }

                    case 'e':
                    {
                        process_exit+=1;
                        
                        // On process exit (instruction), you have to traverse the active process’s page table starting from 0..63 and for each valid entry UNMAP the page and FOUT modified filemapped pages. Note that dirty non-fmapped (anonymous) pages are not written back (OUT) as the process exits. The used frame has to be returned to the free pool and made available to the get_frame() function again. The frames then should be used again in the order they were released.
                        if (o_flag) cout<<"EXIT current process "<<proc<<endl;
                        for (int i=0;i<page_table_size;i++)
                        {
                            Process::page_table_entry * p;
                            p = &current->page_table[i];
                            if (is_valid_vma(i, current))
                            {
                                if (p->present==1)
                                {
                                    p->present = 0;
                                    frame_table_entry * fte;
                                    fte = &frame_table[p->frame_number];
                                    fte->mapped = false; 
                                    if (o_flag) cout<<" UNMAP "<<fte->process_number<<":"<<fte->vpage<<endl;
                                    process_list[current->process_id]->s.unmaps++;
                                    if (p->modified && p->file_mapped)
                                    {
                                        if (o_flag) cout<<" FOUT"<<endl;
                                        process_list[current->process_id]->s.fouts++;
                                    }
                                    free_list.push_back(fte);
                                }
                            }
                            p->paged_out = 0;
                        }
                        break;
                    }

                    case 'r':
                    {
                        total_cost+=1;
                        Process::page_table_entry * pte;
                        pte = &(current->page_table[proc]);
                        if (!pte->present)
                        {
                            // check if valid and set writeprotect and filemapped bits if valid
                            if (is_valid_vma(proc, current))
                            {
                                pte->referenced =1;
                                pte->valid = 1;
                                pte->file_mapped = get_file_mapped(proc, current);
                                pte->write_protect = get_write_protect(proc, current);
                                frame_table_entry * fte;
                                fte = get_frame(pager);
                                if (debug)
                                {
                                    cout<<"GOT FRAME"<<endl;
                                    fflush(stdout);
                                }
                                if (fte->mapped==true)
                                {
                                    if (o_flag==true) cout<<" UNMAP "<<fte->process_number<<":"<<fte->vpage<<endl;
                                    process_list[fte->process_number]->s.unmaps += 1;
                                    fte->page_table_entry->present = 0;
                                    if (fte->page_table_entry->modified == 1)
                                    {
                                        if (fte->page_table_entry->file_mapped == 1)
                                        {
                                            if (o_flag==true) cout<<" FOUT"<<endl;
                                            process_list[fte->process_number]->s.fouts += 1;
                                        }
                                        else
                                        {
                                            if (o_flag==true) cout<<" OUT"<<endl;
                                            fte->page_table_entry->paged_out = 1;     
                                            process_list[fte->process_number]->s.outs += 1;
                                        }
                                        fte->page_table_entry->modified = 0;
                                    }
                                }
                               
                                if (pte->file_mapped == 1)
                                {
                                    if (o_flag==true)  cout<<" FIN"<<endl;
                                    current->s.fins++;
                                }
                                else if (pte->paged_out == 1)
                                {
                                    if (o_flag==true)  cout<<" IN"<<endl;
                                    current->s.ins += 1;
                                }
                                else
                                {
                                    if (o_flag==true)  cout<<" ZERO"<<endl;
                                    current->s.zeros++;
                                }
                                pte->frame_number = fte->frame_number;
                                pte->present = 1;
                                pte->referenced = 1;
                                fte->mapped = true;
                                fte->vpage = proc;
                                fte->page_table_entry = pte;
                                fte->process_number = current->process_id;
                                if (o_flag==true)  cout<<" MAP "<<fte->frame_number<<endl;
                                current->s.maps++;
                                fte = pager->reset_age(fte);
                            }
                            else
                            {
                                // SEGMENTATION FAULT
                                if (o_flag) cout<<" SEGV"<<endl;
                                current->s.segv++;
                                continue;
                            }
                        }
                        else
                        {
                            // PTE PRESENT 
                            pte->referenced =1;
                            continue;
                        }
                        break;
                    }

                    case 'w':
                    {
                        total_cost+=1;
                        Process::page_table_entry * pte = &current->page_table[proc];
                        if (!pte->present)
                        {
                            // check if valid and set writeprotect and filemapped bits if valid
                            if (is_valid_vma(proc, current))
                            {
                                pte->referenced = 1;
                                pte->valid = 1;
                                pte->file_mapped = get_file_mapped(proc, current);
                                pte->write_protect = get_write_protect(proc, current);
                                
                                frame_table_entry * fte;
                                fte = get_frame(pager);
                                if (fte->mapped==true)
                                {
                                    // unmap
                                    if (o_flag) cout<<" UNMAP "<<fte->process_number<<":"<<fte->vpage<<endl;
                                    process_list[fte->process_number]->s.unmaps++;
                                    fte->page_table_entry->present = 0;
                                    if (fte->page_table_entry->modified == 1)
                                    {
                                        if (fte->page_table_entry->file_mapped == 1)
                                        {
                                            if (o_flag) cout<<" FOUT"<<endl;
                                            process_list[fte->process_number]->s.fouts++;
                                        }
                                        else
                                        {
                                            if (o_flag) cout<<" OUT"<<endl;
                                            fte->page_table_entry->paged_out = 1;
                                            process_list[fte->process_number]->s.outs++;
                                        }
                                        fte->page_table_entry->modified = 0;
                                    }
                                }
                                if (pte->paged_out == 1)
                                {
                                    if (pte->file_mapped == 1)
                                    {
                                        if (o_flag) cout<<" FIN"<<endl;
                                        current->s.fins++;
                                    }
                                    else
                                    {
                                        if (o_flag) cout<<" IN"<<endl;
                                        current->s.ins++;
                                    }
                                }
                                else
                                {
                                    if (pte->file_mapped == 1)
                                    {
                                        if (o_flag) cout<<" FIN"<<endl;
                                        current->s.fins++;
                                    }
                                    else
                                    {
                                        if (o_flag) cout<<" ZERO"<<endl;
                                        current->s.zeros++;
                                    
                                    }
                                }
                                pte->frame_number = fte->frame_number;
                                pte->referenced = 1;
                                pte->present = 1;
                                fte->mapped = true;
                                fte->vpage = proc;
                                fte->page_table_entry = pte;
                                fte->process_number = current->process_id;
                                if (o_flag) cout<<" MAP "<<fte->frame_number<<endl;
                                current->s.maps++;
                                fte = pager->reset_age(fte);
                                if (pte->write_protect == 1)
                                {
                                    if (o_flag) cout<<" SEGPROT"<<endl;
                                    current->s.segprot++;
                                    break;
                                }
                                else
                                {
                                    pte->modified = 1;
                                }
                            }
                            else
                            {
                                // SEGMENTATION FAULT
                                if (o_flag) cout<<" SEGV"<<endl;
                                current->s.segv++;
                                break;
                            }
                        }
                        else
                        {
                            pte->referenced =1;
                            if (pte->write_protect == 1)
                                {
                                    if (o_flag) cout<<" SEGPROT"<<endl;
                                    current->s.segprot++;
                                    break;
                                }
                                else
                                {
                                    pte->modified = 1;
                                }
                            pte->modified = 1;
                            break;
                        }
                        break;
                    }
                }
            }
        }
    return;
}

int main(int argc, char** argv)
{
    int index;
    int c;
    string sched;
    char * options;
    opterr = 0;
    context_switch=0;
    process_exit=0;
    map_count=0;
    unmap_count=0;
    ins_count=0;
    fins_count=0;
    outs_count=0;
    fouts_count=0;
    zeros_count=0;
    segv_count=0;
    segp_count=0;
    total_cost = 0;
    instruction_count=0;
    
    while ((c = getopt(argc, argv, "f:a:o:")) != -1)
    {
        if (c=='f')
        {
            max_frame_table_size = atoi(optarg);
        }
        else if (c=='o')
        {
            options = optarg;
        }
        else if (c=='a')
        {
            sched = string(optarg);
        }
        else
        {
            cout<<"Wrong CMD Input!"; 
        }
    }
    
    index = optind;
    string file(argv[index]);
    index++;
    string rfile_path(argv[index]);
    frame_table = new frame_table_entry[max_frame_table_size];
    
    for (int i=0;i<strlen(options);i++)
    {
            if (options[i]=='F')
            {
                f_flag = true;
            }
            if (options[i]=='O')
            {
                o_flag = true;
            }
            if (options[i]=='P')
            {
                p_flag = true;
            }
            if (options[i]=='S')
            {
                s_flag = true;
            }
    }
    Utility_Functions * u = new Utility_Functions();
    u->initialise_myfile(file);
    initialise_random_list(rfile_path);
    initialise_frame_table();
    u->initialise_free_list();

    Pager * p = u->get_pager_object(sched);
    if (myfile.is_open())
    {
        string line;
        while (!myfile.eof())
        {
            line = u->get_line();
            while (line[line.find_first_not_of(' ')] == '#')
            {
                line = u->get_line();
            }
            if (debug) cout<<line<<endl;
            int process_count = stoi(line);
            for (int i=0;i<process_count;i++)
            {
                u->read_processes(i);
            }
            simulation(p);
            if (p_flag == true)
            {
                u->print_process_page_table();
            }
            if (f_flag)
            {
                u->print_frame_table();
            }
            if (s_flag)
            {
                u->calculate_total_cost();
                printf("TOTALCOST %lu %lu %lu %lu %lu\n", instruction_count,context_switch,process_exit,total_cost, sizeof(Process::page_table_entry) );
            }
            free(frame_table);
        }
    }
    else
    {
        cout<<"File not found"<<endl;
    }
    return 0;
}


