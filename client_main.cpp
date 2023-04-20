#include <iostream>
#include <fstream>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <vector>
#include <sstream>
#include <thread>
#include <cstring>
#include <map>
#include <mutex>

using namespace std;

bool should_be_running[512];
map<string, mutex*> file_locks;

struct command_string
{
    string token1;
    string token2;
    string token3;
};

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
    cout << "[DEBUG] Deleted: " << file_name << "\n";
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
    char buffer[1024] = {};
    int size = 0;
    int corrected;
    if (read(sock, &size, sizeof(int)) == 0)
    {
        cout << "Unable to read size of message" << endl;
    }
    corrected = ntohl(size);
    if (read(sock, buffer, corrected) < 0)
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
    //if (bytes_read != sizeof(int)) cout << "[ERROR] Unable to read message\n";
    int out = ntohl(received_int);
    return out;
}


// Check if file_name exists in the client's directory
bool file_exists(string file_name) {
    ifstream file(file_name.c_str());
    return file.good();
}


int get_file2(int sock, string file_name, bool background)
{
    fstream file;
    const string local_fname = file_name;

    // get command ID and print
    int cmd_id = get_message2(sock);
    should_be_running[cmd_id] = true;
    if (background)
    {
        cout << "\nCommand ID: " << cmd_id << endl;
    }
    

    //tell server what file name to get
    send_message(sock, local_fname);

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
    
    int total = 0;

    //int bytes_to_read = get_message2(sock);
    
    while (1) 
    {
        int bytes_to_read = get_message2(sock); //cout << "bytes_to_read = " << bytes_to_read << "\n";
        string buffer;
        buffer.resize(bytes_to_read);
        if (bytes_to_read < 1) break;
        if (bytes_to_read > 1024) 
        {
            cout << "[ERROR] Network error\n";
            return -1;
        }
        if (get_message2(sock) != 1)
        {
            cout << "Server Terminated get\n";
            //close(sock);
            simp_delete(file_name);
            return -1;
        }
        
        int bytes_received = 0;
        while (bytes_received < bytes_to_read)
        {
            int result = read(sock, &buffer[bytes_received], bytes_to_read - bytes_received);
            bytes_received += result;
        } 
        total += bytes_received;
        
        file.write(buffer.c_str(), bytes_received);
    
        
       // cout << "bytes_received = " << bytes_received<< "\n";
        
    }
    //this way the reader knows if the sender finished due to term cmd
    int success = get_message2(sock);
    https://scholar.google.com/citations?user=tBIAgtgAAAAJ
    //when done
    should_be_running[cmd_id] = false;

    return success;
}



int put_file2(int sock, string file_name, bool background)
{
    const string local_fname = file_name;

    // get command ID and print
    int cmd_id = get_message2(sock);

    should_be_running[cmd_id] = true;
    if (background)
    {
        cout << "\nCommand ID: " << cmd_id << endl;
    }

    //tell server what file name to open
    send_message(sock, local_fname);
    
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

    char buffer[1024] = {0};
    while (!file.eof()) 
    {
        file.read(buffer, 1024);
        int bytes_read = file.gcount();
        int bytes_sent = 0;
        
        if (bytes_read <= 0) break;
        while (bytes_sent < bytes_read) 
        {
            if (send_message2(sock, bytes_read) != 4) cout << "[ERROR] Network problems\n";
            if (should_be_running[cmd_id] == false)
            {
                cout << "Terminated put\n";
                //close(sock);
                return -1;
            }
            int result = send(sock, buffer + bytes_sent, bytes_read - bytes_sent, 0);
            bytes_sent += result;
        }

        memset(buffer, 0, 1024);
    }
    //send 0 to mark complete (EOF)
    send_message2(sock, 0);

    
    int success = get_message2(sock);
    
    should_be_running[cmd_id] = false;
    return success;
}


command_string parse_com(string input)
{
    struct command_string cs{"x", "x"};
    string temp;
    stringstream ss(input);
    vector <string> token;
    while(getline(ss, temp, ' '))
    {
        token.push_back(temp);
    }
    
    // some bad error handling
    if (token.size() == 3)
    {
        cs.token1 = token[0];
        cs.token2 = token[1];
        cs.token3 = token[2];
    }

    if (token.size() == 2)
    {
        cs.token1 = token[0];
        cs.token2 = token[1];
    }
    
    if (token.size() == 1)
        cs.token1 = token[0];

    return cs;
}


int term_handler(int t_port,  char *ip_name, string term_msg)
{
    int address_length, sock;
    struct sockaddr_in address;
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) 
    { 
        cout << "Term socket creation failed." << endl;
        exit(0);
    }

    address.sin_family = AF_INET;
    address.sin_port = htons(t_port);
    address_length = sizeof(address);

    if(inet_pton(AF_INET, ip_name, &address.sin_addr)<=0) 
    { 
        cout << "Term socket address failed." << endl;
        exit(0);
    }

    if (connect(sock, (struct sockaddr *)&address, address_length) < 0) 
    { 
        cout << "Term socket connection failed." << endl;
        exit(0);
    }
    should_be_running[atoi(term_msg.c_str())] = false;
    send_message(sock, term_msg); 
    close(sock);
    return 0;
}

//thread handlers for "&" tasks
//spins off a new thread and a new socket
//big ugly copy of the menu logic in main
void background_handler(char *ip, int port, command_string commands)
{
    int sock, address_length;
    string error, cmd_id;
    struct sockaddr_in address;
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) 
    { 
        cout << "Background socket/thread socket creation failed." << endl;
        exit(0);
    }

    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address_length = sizeof(address);

    if(inet_pton(AF_INET, ip, &address.sin_addr)<=0) 
    { 
        cout << "Background socket/thread address failed." << endl;
        exit(0);
    }


    if (connect(sock, (struct sockaddr *)&address, address_length) < 0) 
    { 
        cout << "Background socket/thread connection failed." << endl;
        exit(0);
    }

    //cout << "[DEBUG] Background socket/thread created\n";

    //send_message(sock, "&");
    //cmd_id = get_message(sock);
    //cout << "Command ID is " << cmd_id << endl;

    if (commands.token1 == "quit")
    {
            send_message(sock, "quit");
    }
    else if (commands.token1 == "ls")
    {
        send_message(sock, "ls");
        cout << get_message(sock) << endl;
    }
    else if (commands.token1 == "pwd")
    {
        send_message(sock, "pwd");
        cout << get_message(sock) << endl;
    }
    else if(commands.token1 == "get" || commands.token1 == "put" || commands.token1 == "delete" || commands.token1 == "cd" || commands.token1 == "mkdir") {
        send_message(sock, "pwd");
        string path = get_message(sock) + "/" + commands.token2;
        cout << path << endl;
        file_locks.emplace(path, new mutex());
        file_locks.at(path)->lock();
        if (commands.token1 == "get")
        {
                bool file_on_client, file_on_server;
            // Start the get handler
            send_message(sock, "get");

            // // Check if the file is present on the client
            // if(file_exists(commands.token2)) {
            //     file_on_client = true;
            // } else {
            //     file_on_client = false;
            // }

            // // Check if the file is present on the server
            // send_message(sock, commands.token2);
            // if(get_message(sock).compare("true") == 0) {
            //     file_on_server = true;
            // } else {
            //     file_on_server = false;
            // }

            // // Run the command if appropriate
            // if(file_on_server && !file_on_client) {
            //     send_message(sock, "run");
            //     get_file2(sock, commands.token2, true);
            // } else {
            //     send_message(sock, "don't run");
            //     cout << "Unable to get " << commands.token2 << "." << endl;
            // }
            get_file2(sock, commands.token2, true);
        }
        else if (commands.token1 == "put")
        {
            bool file_on_client, file_on_server;
            // Start the put handler
            send_message(sock, "put");

            // // Check if the file is present on the client
            // if(file_exists(commands.token2)) {
            //     file_on_client = true;
            // } else {
            //     file_on_client = false;
            // }

            // // Check if the file is present on the server
            // send_message(sock, commands.token2);
            // if(get_message(sock).compare("true") == 0) {
            //     file_on_server = true;
            // } else {
            //     file_on_server = false;
            // }

            // // Run the command if appropriate
            // if(!file_on_server && file_on_client) {
            //     send_message(sock, "run");
            //     put_file2(sock, commands.token2, true);
            // } else {
            //     send_message(sock, "don't run");
            //     cout << "Unable to put " << commands.token2 << "." << endl;
            // }
            put_file2(sock, commands.token2, true);

        }
        else if (commands.token1 == "delete")
        {
            send_message(sock, "delete");
            send_message(sock, commands.token2);
            error = get_message(sock);
            if(error.compare("true") == 0)
                cout << get_message(sock) << endl;
        }
        else if (commands.token1 == "cd")
        {
            send_message(sock, "cd");
            send_message(sock, commands.token2);
            error = get_message(sock);
            if(error.compare("true") == 0)
                cout << get_message(sock) << endl;
        }
        else if (commands.token1 == "mkdir")
        {
            send_message(sock, "mkdir");
            send_message(sock, commands.token2);
            error = get_message(sock);
            if(error.compare("true") == 0)
                cout << get_message(sock) << endl;
        }
        file_locks.at(path)->unlock();
    }
    else
    {
        cout << "Unknown option, try again..." << endl;
    }
    send_message(sock, "silent_quit");
    close(sock);
}


int main(int argc, char **argv)
{
    char *ip_name;
    int n_port, t_port, sock, address_length;
    struct sockaddr_in address;
    string cmd_id = "x";

    if (argc != 4)
    {
        cout << "Usage: ./myftp <IP> <n_port> <t_port>" << endl;
        exit(0);
    }


    ip_name = argv[1];
    n_port = atoi(argv[2]);
    t_port = atoi(argv[3]);

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) 
    { 
        cout << "Socket creation failed." << endl;
        exit(0);
    }
            
    address.sin_family = AF_INET;
    address.sin_port = htons(n_port);
    address_length = sizeof(address);

    if(inet_pton(AF_INET, ip_name, &address.sin_addr)<=0) 
    { 
        cout << "Address failed." << endl;
        exit(0);
    }


    if (connect(sock, (struct sockaddr *)&address, address_length) < 0) 
    { 
        cout << "Connection failed." << endl;
        exit(0);
    }

    cout << "Connection Successful to " << ip_name << " on port " << n_port << "." <<endl;

    //hacky work around
    for (int i = 0; i < 512; i++)
    {
        should_be_running[i] = false;
    }


    //menu
    string input, token1, token2, error;
    bool done = false;
    struct command_string commands{"x","x","x"}; //defaults, can be used to check if something was done
    while (!done)
    {   
        cout << "myftp> ";
        getline(cin, input);
        commands = parse_com(input);
        if (commands.token3 == "&")
        {
            // cout << "[DEBUG] Moving command to background thread/socket\n";
            thread back_thread(background_handler, ip_name, n_port, commands);
            back_thread.detach();
        }
        else
        {
            if (commands.token1 == "quit")
            {
                send_message(sock, "quit");
                done = true;
            }
            else if (commands.token1 == "get")
            {
                bool file_on_client, file_on_server;
                // Start the get handler
                send_message(sock, "get");

                // // Check if the file is present on the client
                // if(file_exists(commands.token2)) {
                //     file_on_client = true;
                // } else {
                //     file_on_client = false;
                // }

                // // Check if the file is present on the server
                // send_message(sock, commands.token2);
                // if(get_message(sock).compare("true") == 0) {
                //     file_on_server = true;
                // } else {
                //     file_on_server = false;
                // }

                // // Run the command if appropriate
                // if(file_on_server && !file_on_client) {
                //     send_message(sock, "run");
                //     get_file2(sock, commands.token2, false);
                // } else {
                //     send_message(sock, "don't run");
                //     cout << "Unable to get " << commands.token2 << "." << endl;
                // }
                get_file2(sock, commands.token2, false);
            }
            else if (commands.token1 == "put")
            {
                bool file_on_client, file_on_server;
                // Start the put handler
                send_message(sock, "put");

                // // Check if the file is present on the client
                // if(file_exists(commands.token2)) {
                //     file_on_client = true;
                // } else {
                //     file_on_client = false;
                // }

                // // Check if the file is present on the server
                // send_message(sock, commands.token2);
                // if(get_message(sock).compare("true") == 0) {
                //     file_on_server = true;
                // } else {
                //     file_on_server = false;
                // }

                // // Run the command if appropriate
                // if(!file_on_server && file_on_client) {
                //     send_message(sock, "run");
                //     put_file2(sock, commands.token2, false);
                // } else {
                //     send_message(sock, "don't run");
                //     cout << "Unable to put " << commands.token2 << "." << endl;
                // }
                put_file2(sock, commands.token2, false);
            }
            else if (commands.token1 == "delete")
            {
                send_message(sock, "delete");
                send_message(sock, commands.token2);
                error = get_message(sock);
                if(error.compare("true") == 0)
                    cout << get_message(sock) << endl;
            }
            else if (commands.token1 == "ls")
            {
                send_message(sock, "ls");
                cout << get_message(sock) << endl;
            }
            else if (commands.token1 == "cd")
            {
                send_message(sock, "cd");
                send_message(sock, commands.token2);
                error = get_message(sock);
                if(error.compare("true") == 0)
                    cout << get_message(sock) << endl;
            }
            else if (commands.token1 == "mkdir")
            {
                send_message(sock, "mkdir");
                send_message(sock, commands.token2);
                error = get_message(sock);
                if(error.compare("true") == 0)
                    cout << get_message(sock) << endl;
            }
            else if (commands.token1 == "pwd")
            {
                send_message(sock, "pwd");
                cout << get_message(sock) << endl;
            }
            else if (commands.token1 == "terminate")
            {
                term_handler(t_port, ip_name, commands.token2);  
            }
            else if (commands.token1.length() <= 1)
            {
                // catch return inputs
            }
            else
            {
                cout << "Unknown option, try again..." << endl;
            }
        }
    }
    close(sock);

    return 0;
}