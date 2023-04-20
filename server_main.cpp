#include <cstddef>
#include <iostream>
#include <string>
#include <fstream>
#include <dirent.h>
#include <string>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <vector>
#include <sstream>
#include <thread>
#include <mutex>
#include <cstring>
#include <sys/sendfile.h>

using namespace std;

mutex cmd_table_lock;
mutex kill_table_lock;

bool cmd_table[512]; // true indicates a get/put is currently running with that ID
bool kill_table[512]; // true indicates a get/put with that ID is to be terminated


//from srv_pwd
string simp_pwd() {
    char buffer[1025] = {};
    getcwd(buffer, 1024);
    string pwd(buffer);
    return pwd;
}

//from srv_delete
int simp_delete(string file_name) {
    string abs_file_name = simp_pwd() + "/" + file_name;
    remove(abs_file_name.c_str());
    //cout << "[DEBUG] Deleted: " << file_name << "\n";
    return 0;
}

//sends a string to the other party's get_message
int send_message(int sock, string mess)
{
    int length = mess.length();
    int size = htonl(length);
    int sent = send(sock, &size, sizeof(int), 0); 
    sent = send(sock, mess.c_str(), mess.length(), 0); 
    return sent;
}

//sends an int
int send_message2(int sock, int msg)
{
    int num_in = msg;
    int num_out = htonl(num_in);
    int bytes_written = write(sock, &num_out, sizeof(int));
    //cout << "Sent: "<< num_in << "\n";
    return bytes_written;
}

//returns a string from the other party's send_message
string get_message(int sock)
{
    string input;
    char buffer[1025] = {};
    int size = 0;
    int corrected;
    if (read(sock, &size, sizeof(int)) == 0)
    {
        cout << "Unable to read size of message" << endl;
    }
    corrected = ntohl(size);
    if (read(sock, buffer, corrected) == 0)
    {
        cout << "Unable to read message" << endl;
    }
    input = buffer;
    return input;
}

//gets an int
int get_message2(int sock)
{
    int received_int = 0;
    int bytes_read = read(sock, &received_int, sizeof(int));
    if (bytes_read != sizeof(int)) cout << "[ERROR] Unable to read message "<< bytes_read <<"\n";
    int out = ntohl(received_int);
    return out;
}

string srv_pwd() {
    char buffer[1025] = {};
    getcwd(buffer, 1024);
    string pwd(buffer);
    return pwd;
}

string srv_ls() {
    string ls = "";
    DIR *dir;
    struct dirent *read;
    if ((dir = opendir(srv_pwd().c_str())) != nullptr) {
        while((read = readdir(dir)) != nullptr) {
            if(read->d_name[0] != '.') {
                ls = ls + read->d_name + " ";
            }
        }
        closedir(dir);
    } else {
        cout << "Could not open current directory." << endl;
        exit(0);
    }
    return ls;
}

void file_exists(int sock) {
    string file_name = get_message(sock);
    ifstream file(file_name.c_str());

    if(file.good()) {
        send_message(sock, "true");
    } else {
        send_message(sock, "false");
    }
    file.close();
}


// gets an id from the command table and returns it
int get_cmd_id()
{
    int i;
    for(i = 0; i < 512; i++) {
        if(cmd_table[i] == false) {
            //cout << "CMD TABLE " << i << " true" << endl;
            cmd_table_lock.lock();
            cmd_table[i] = true;
            cmd_table_lock.unlock();
            return i;
        }
    }
    return -1;
}



int srv_get_file2(int sock)
{
    int cmd_id = get_cmd_id();
    send_message2(sock, cmd_id);

    //get file name
    string file_name = get_message(sock);
    //cout << "Got file name\n";
    
    ifstream file(file_name.c_str(), ios::in | ios::binary);
    if(file.is_open())
    {
        //cout << "File " << file_name << " opened" << endl;
    }
    else
    {
        cout << "Unable to open " << file_name << endl;
        exit(0);
    }

    cout << "[LOG] File <" << file_name << "> get started\n";
    char buffer[1024] = {0};
    while (!file.eof()) 
    {
        file.read(buffer, 1024);
        int bytes_read = file.gcount();
        int bytes_sent = 0;
        
        if (bytes_read <= 0) break;
        while (bytes_sent < bytes_read) 
        {
            send_message2(sock, bytes_read);
            //cout << "bytes_read = " << bytes_read << "\n";
            if (kill_table[cmd_id] == true)
            {
                send_message2(sock, -1);
                //cout << "CMD & KILL TABLE " << cmd_id << " false" << endl;
                kill_table_lock.lock();
                cmd_table_lock.lock();
                kill_table[cmd_id] = false;
                cmd_table[cmd_id] = false;
                kill_table_lock.unlock();
                cmd_table_lock.unlock();
                return -1;
            }
            else
            {
                send_message2(sock, 1);
            }
            int result = send(sock, buffer + bytes_sent, bytes_read - bytes_sent, 0);
            bytes_sent += result;
        }

        memset(buffer, 0, 1024);
    }

    //send 0 to mark complete
    send_message2(sock, 0);

    //send 1 to mark complete
    send_message2(sock, 1);
    //cout << "CMD & KILL TABLE " << cmd_id << " false" << endl;
    kill_table_lock.lock();
    cmd_table_lock.lock();
    kill_table[cmd_id] = false;
    cmd_table[cmd_id] = false;
    kill_table_lock.unlock();
    cmd_table_lock.unlock();
    cout << "[LOG] File <" << file_name << "> get finished\n";
    return 0;
}


int srv_put_file2(int sock)
{
    fstream file;
    int cmd_id = get_cmd_id();
    send_message2(sock, cmd_id);

    //get file name
    string file_name = get_message(sock);


    //int f_size = get_message2(sock);

    file.open(file_name.c_str(), ios::out | ios::trunc | ios::binary);
    if(file.is_open())
    {
        //cout << "Opened file " << file_name << endl;
    }
    else
    {
        cout << "Could not open " << file_name << endl;
        exit(0);
    }
    
    cout << "[LOG] File <" << file_name << "> put started\n";
    int total = 0;
    int bytes_received, result, bytes_to_read;
    
    while (1) 
    {
        bytes_to_read = get_message2(sock);
       
        //send cont value
        if (kill_table[cmd_id] == true)
        {
            //send_message2(sock, -1);
            //cout << "CMD & KILL TABLE " << cmd_id << " false" << endl;
            kill_table_lock.lock();
            cmd_table_lock.lock();
            kill_table[cmd_id] = false;
            cmd_table[cmd_id] = false;
            kill_table_lock.unlock();
            cmd_table_lock.unlock();
            simp_delete(file_name);
            return -1;
        }
        else
        {
            //send_message2(sock, 1);
        }
        if (bytes_to_read < 1) 
        {
            //cout << "bytes_to_read" << bytes_to_read << "\n";
            break;
        }
        if (bytes_to_read > 1024) 
        {
            cout << "[ERROR] Network error\n";
            return -1;
        }
        string buffer;
        buffer.resize(bytes_to_read);

        bytes_received = 0;
        while (bytes_received < bytes_to_read)
        {
            result = read(sock, &buffer[bytes_received], bytes_to_read - bytes_received);
            bytes_received += result;
        } 
        total += bytes_received;
        file.write(buffer.c_str(), bytes_received);
    }
    send_message2(sock, 1);
    //cout << "CMD & KILL TABLE " << cmd_id << " false" << endl;
    kill_table_lock.lock();
    cmd_table_lock.lock();
    kill_table[cmd_id] = false;
    cmd_table[cmd_id] = false;
    kill_table_lock.unlock();
    cmd_table_lock.unlock();
    

    cout << "[LOG] File <" << file_name << "> put finished\n";
    return 0;
}


int srv_delete(int sock) {
    string file_name = get_message(sock);
    string abs_file_name = srv_pwd() + "/" + file_name;
    if(remove(abs_file_name.c_str()) == 0) {
        send_message(sock, "false");
    } else {
        send_message(sock, "true");
        send_message(sock, "File " + file_name + " could not be deleted.");
    }
    return 0;
}

int srv_mkdir(int sock) {
    string file_name = get_message(sock);
    string abs_file_name = srv_pwd() + "/" + file_name;
    if(mkdir(abs_file_name.c_str(), 0757) == 0) {
        send_message(sock, "false");
    } 
    else {
        send_message(sock, "true");
        send_message(sock, "Directory " + file_name + " could not be created.");
    }
    return 0;
}

int srv_cd(int sock) {
    string file_name = get_message(sock);
    string abs_file_name = srv_pwd() + "/" + file_name;
    if(chdir(abs_file_name.c_str()) == 0) {
        send_message(sock, "false");
    } else {
        send_message(sock, "true");
        send_message(sock, "Could not change to directory " + file_name + ".");
    }
    return 0;
}

//main handler for commnunications
int wait_for_comm(int sock)
{
    string input, arg;
    input = get_message(sock);
    if (input == "quit")
    {
        cout << "Client has left" << endl;
        return 1;
    }
    else if(input == "silent_quit")
    {
        return 1;
    }
    else if (input == "get")
    {
        //file_exists(sock);

        //if(get_message(sock).compare("run") == 0) {
            srv_get_file2(sock);
        //}
    }
    else if (input == "put")
    {
        //file_exists(sock);

        //if(get_message(sock).compare("run") == 0) {
            srv_put_file2(sock);
        //}
    }
    else if (input == "delete")
    {
        srv_delete(sock);
    }
    else if (input == "ls")
    {
        send_message(sock, srv_ls());
    }
    else if (input == "cd")
    {
        srv_cd(sock);
    }
    else if (input == "mkdir")
    {
        srv_mkdir(sock);
    }
    else if (input == "pwd")
    {
        send_message(sock, srv_pwd());
    }
    else if (input == "&")
    {
        //send_message(sock, get_cmd_id()); // send a command id
        //cout << "[Client requested CMD ID]\n";
    }
    else
    {
        cout << "Server was sent a bad command" << endl;
    }
    
    return 0;
}

//normal worker for clients, basically runs an input handler loop
int norm_worker(int sock)
{
    unshare(CLONE_FS); //get fs per thread
    while (!wait_for_comm(sock)){};
    close(sock);
    return 0;
}

//term worker for handling term messages, this way you could have multiple term messages at once
int term_worker(int sock)
{
    // logic for changing the state of the command table would be called here instead of the debug msg
    //cout << "[DEBUG TERM MSG]: " << get_message(sock) << "\n"; 
    int index = atoi(get_message(sock).c_str());
    if(cmd_table[index] == true) {
        //cout << "KILL TABLE " << index << " true" << endl;
        kill_table_lock.lock();
        kill_table[index] = true;
        kill_table_lock.unlock();
    } else {
        cout << "Command ID " << index << " is not in use." << endl;
    }
    close(sock);
    return 0;
}

//watcher thread for the normal port, spins up normal workers to handle clients
int norm_watcher(int port)
{
    
    int thread_num = 0;
    int list_sock, sock, address_length;

    struct sockaddr_in address;
    if ((list_sock = socket(AF_INET, SOCK_STREAM, 0)) == 0) 
    { 
        cout << "Socket(norm) failed" << endl;
        exit(0);
    }

    address.sin_family = AF_INET; 
    address.sin_addr.s_addr = INADDR_ANY; 
    address.sin_port = htons(port);
    address_length = sizeof(address);

    if (bind(list_sock, (struct sockaddr *)&address, address_length)<0) 
    {
        cout << "Unable to bind socket(norm)" << endl;
        exit(0);
    }
    cout << "[Normal watcher running]\n";
    while (1)
    {
        if (listen(list_sock, 1) < 0) 
        {
            cout << "Norm listen failed." << endl;
            exit(0);
        }

        if ((sock = accept(list_sock, (struct sockaddr *)&address, (socklen_t*)&address_length))<0) 
        { 
            cout << "Accept failed.(norm)" << endl;
            exit(0);
        }
        
        thread nw_thread(norm_worker, sock);
        nw_thread.detach();
        thread_num++;
    }
    return 0;
}

//watcher thread for the term port, spins up temp term workers to handle term messages
int term_watcher(int port)
{ 
    int thread_num = 0;
    int list_sock, sock, address_length;

    struct sockaddr_in address;
    if ((list_sock = socket(AF_INET, SOCK_STREAM, 0)) == 0) 
    { 
        cout << "Socket(term) failed" << endl;
        exit(0);
    }

    address.sin_family = AF_INET; 
    address.sin_addr.s_addr = INADDR_ANY; 
    address.sin_port = htons(port);
    address_length = sizeof(address);

    if (bind(list_sock, (struct sockaddr *)&address, address_length)<0) 
    {
        cout << "Unable to bind socket(term)" << endl;
        exit(0);
    }
    cout << "[Terminate watcher running]\n";
    while (1)
    {
        if (listen(list_sock, 1) < 0) 
        {
            cout << "Term listen failed." << endl;
            exit(0);
        }
    
        if ((sock = accept(list_sock, (struct sockaddr *)&address, (socklen_t*)&address_length))<0) 
        { 
            cout << "Accept failed.(term)" << endl;
            exit(0);
        }
        
        thread tr_thread(term_worker, sock);
        tr_thread.detach();
        thread_num++;
    }
    return 0;
}


//handler function to start the two watcher threads, blocks on watchers
int handler(int n_port, int t_port)
{
    thread norm_watch_thread(norm_watcher, n_port);

    thread term_watch_thread(term_watcher, t_port);

    cout << "[Server running]" << endl;
    cout << "Ports: " << n_port << " " << t_port << endl;

    norm_watch_thread.join();
    term_watch_thread.join();
    
    return 0;
}

int main(int argc, char **argv)
{
    int n_port, t_port;

    //int i;
    //for(i = 0; i < 512; i++) cmd_table[i] == false;

    if (argc != 3)
    {
        cout << "Usage: ./myftpserver <n_port> <t_port>" << endl;
        exit(0);
    }
    n_port = atoi(argv[1]);
    t_port = atoi(argv[2]);
    cout << "[Server started]" << endl;
    handler(n_port, t_port);
    return 0;
}
