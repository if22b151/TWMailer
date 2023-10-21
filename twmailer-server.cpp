#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

///////////////////////////////////////////////////////////////////////////////
#define BUF 1024

///////////////////////////////////////////////////////////////////////////////

int abortRequested = 0;
int create_socket = -1;
int new_socket = -1;

///////////////////////////////////////////////////////////////////////////////

void createDir(const std::string& receiverDir);
void writeIntoFile(std::vector<std::string>& parts, const std::string& receiverDir);
void updateIndex(const std::string& filePath, int index);
void deleteFile(const std::string& pathToFileToDelete);
std::string readFile(const std::string& pathToFileToRead);
std::string listFiles(const std::string& receive);
void *clientCommunication(void *data, std::string spoolDirectory);
void signalHandler(int sig);

///////////////////////////////////////////////////////////////////////////////

int main(int argc, char *argv[])
{
    socklen_t addrlen;
    struct sockaddr_in address, cliaddress;
    int reuseValue = 1;
    std::cout << argv[0] << '\n';
    std::cout << argv[1] << '\n';
    std::cout << argv[2] << '\n';

    ////////////////////////////////////////////////////////////////////////////
    // SIGNAL HANDLER
    // SIGINT (Interrup: ctrl+c)
    // https://man7.org/linux/man-pages/man2/signal.2.html
    if (signal(SIGINT, signalHandler) == SIG_ERR)
    {
        perror("signal can not be registered");
        return EXIT_FAILURE;
    }

    ////////////////////////////////////////////////////////////////////////////
    // CREATE A SOCKET
    // https://man7.org/linux/man-pages/man2/socket.2.html
    // https://man7.org/linux/man-pages/man7/ip.7.html
    // https://man7.org/linux/man-pages/man7/tcp.7.html
    // IPv4, TCP (connection oriented), IP (same as client)
    if ((create_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("Socket error"); // errno set by socket()
        return EXIT_FAILURE;
    }

    ////////////////////////////////////////////////////////////////////////////
    // SET SOCKET OPTIONS
    // https://man7.org/linux/man-pages/man2/setsockopt.2.html
    // https://man7.org/linux/man-pages/man7/socket.7.html
    // socket, level, optname, optvalue, optlen
    if (setsockopt(create_socket, SOL_SOCKET, SO_REUSEADDR, &reuseValue, sizeof(reuseValue)) == -1)
    {
        perror("set socket options - reuseAddr");
        return EXIT_FAILURE;
    }

    if (setsockopt(create_socket, SOL_SOCKET, SO_REUSEPORT, &reuseValue, sizeof(reuseValue)) == -1)
    {
        perror("set socket options - reusePort");
        return EXIT_FAILURE;
    }

    ////////////////////////////////////////////////////////////////////////////
    // INIT ADDRESS
    // Attention: network byte order => big endian
    if(argc != 3)
    {
        printf("There are either not enough arguemnts or too many.\nPlease enter port and mail-spool-directoryname\n");
        return EXIT_FAILURE;
    }

    in_port_t port = (in_port_t)std::stol(argv[1]);
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    ////////////////////////////////////////////////////////////////////////////
    // ASSIGN AN ADDRESS WITH PORT TO SOCKET
    if (bind(create_socket, (struct sockaddr *)&address, sizeof(address)) == -1)
    {
        perror("bind error");
        return EXIT_FAILURE;
    }

    ////////////////////////////////////////////////////////////////////////////
    // ALLOW CONNECTION ESTABLISHING
    // Socket, Backlog (= count of waiting connections allowed)
    if (listen(create_socket, 5) == -1)
    {
        perror("listen error");
        return EXIT_FAILURE;
    }

    while (!abortRequested)
    {
        /////////////////////////////////////////////////////////////////////////
        // ignore errors here... because only information message
        // https://linux.die.net/man/3/printf
        printf("Waiting for connections...\n");

        /////////////////////////////////////////////////////////////////////////
        // ACCEPTS CONNECTION SETUP
        // blocking, might have an accept-error on ctrl+c
        addrlen = sizeof(struct sockaddr_in);
        if ((new_socket = accept(create_socket, (struct sockaddr *)&cliaddress, &addrlen)) == -1)
        {
            if (abortRequested)
            {
                perror("accept error after aborted");
            }
            else
            {
                perror("accept error");
            }
            break;
        }

        /////////////////////////////////////////////////////////////////////////
        // START CLIENT
        // ignore printf error handling
        printf("Client connected from %s:%d...\n", inet_ntoa(cliaddress.sin_addr), ntohs(cliaddress.sin_port));
        clientCommunication(&new_socket, argv[2]); // returnValue can be ignored
        new_socket = -1;
    }

    // frees the descriptor
    if (create_socket != -1)
    {
        if (shutdown(create_socket, SHUT_RDWR) == -1)
        {
            perror("shutdown create_socket");
        }
        if (close(create_socket) == -1)
        {
            perror("close create_socket");
        }
        create_socket = -1;
    }

    return EXIT_SUCCESS;
}

void *clientCommunication(void *data, std::string spoolDirectory)
{
    char buffer[BUF];
    int size;
    int *current_socket = (int *)data;

    ////////////////////////////////////////////////////////////////////////////
    // SEND welcome message
    strcpy(buffer, "Welcome to myserver!\r\nPlease enter your commands...\r\n");
    if (send(*current_socket, buffer, strlen(buffer), 0) == -1)
    {
        perror("send failed");
        return NULL;
    }

    do
    {
        /////////////////////////////////////////////////////////////////////////
        // RECEIVE
        size = recv(*current_socket, buffer, BUF - 1, 0);
        if (size == -1)
        {
            if (abortRequested)
            {
                perror("recv error after aborted");
            }
            else
            {
                perror("recv error");
            }
            break;
        }

        if (size == 0)
        {
            printf("Client closed remote socket\n"); // ignore error
            break;
        }

        // remove ugly debug message, because of the sent newline of client
        if (buffer[size - 2] == '\r' && buffer[size - 1] == '\n')
        {
            size -= 2;
        }
        else if (buffer[size - 1] == '\n')
        {
            --size;
        }

        std::string message = "OK";
        buffer[size] = '\0';
        // Read strings separated by '\n' from the input stream
        std::vector<std::string> parts;
        parts.reserve(3);
        std::istringstream stream(buffer); // Create an input string stream from the buffer
        std::string line;
        bool readList = false;
        int counter = 0;
        while (std::getline(stream, line, '\n'))
        {
            // Process the extracted string (in this example, just print it)
            parts.emplace_back(line);
            counter++;
        }

        if(strncmp(buffer, "SEND", 4) == 0)
        {
            if(counter == 5)
            {
                std::string receiverDir = spoolDirectory + "/" + parts[2];
                if(!fs::exists(spoolDirectory))
                    fs::create_directories(spoolDirectory);
                
                if(!fs::exists(receiverDir))
                {
                    createDir(receiverDir);
                }
                else
                {
                    writeIntoFile(parts, receiverDir);
                }
            }
            else
            {
                message = "ERR";
            }
        }
        else if(strncmp(buffer, "LIST", 4) == 0)
        {
            if(counter == 2)
            {
                std::string directoryPath = spoolDirectory + "/" + parts[1];
                if (fs::is_directory(directoryPath)) 
                {
                    message = listFiles(directoryPath);
                    readList = true;
                } 
                else 
                {
                    message = "0 messages";
                }
            }
            else 
            {
                message = "ERR";
            }
        }
        else if(strncmp(buffer, "DEL", 3) == 0)
        {
            if(readList && counter == 3)
            {
                std::string pathToFileToDelete = spoolDirectory + "/" + parts[1] + "/" + parts[2] + ".txt";
                if (fs::exists(pathToFileToDelete)) 
                {
                    deleteFile(pathToFileToDelete);
                } 
                else
                {
                    message = "ERR";
                }
            }
            else
            {
                message = "ERR";
            }
        }
        else if(strncmp(buffer, "READ", 4) == 0)
        {
            if(readList && counter == 3)
            {
                std::string pathToFileToRead = spoolDirectory + "/" + parts[1] + "/" + parts[2] + ".txt";
                if (fs::exists(pathToFileToRead)) 
                {
                    message = readFile(pathToFileToRead);
                } 
                else
                {
                    message = "ERR";
                }
            }
            else
            {
                message = "ERR\n Please open LIST at least one time";
            }
        }
        printf("Message received: %s\n", buffer); // ignore error

        if (send(*current_socket, message.c_str(), message.size() + 1, 0) == -1)
        {
            perror("send answer failed");
            return NULL;
        }
    } while (strcmp(buffer, "quit") != 0 && !abortRequested);

    // closes/frees the descriptor if not already
    if (*current_socket != -1)
    {
        if (shutdown(*current_socket, SHUT_RDWR) == -1)
        {
            perror("shutdown new_socket");
        }
        if (close(*current_socket) == -1)
        {
            perror("close new_socket");
        }
        *current_socket = -1;
    }

    return NULL;
}

void signalHandler(int sig)
{
    if (sig == SIGINT)
    {
        printf("abort Requested... "); // ignore error
        abortRequested = 1;
        /////////////////////////////////////////////////////////////////////////
        // With shutdown() one can initiate normal TCP close sequence ignoring
        // the reference count.
        // https://beej.us/guide/bgnet/html/#close-and-shutdownget-outta-my-face
        // https://linux.die.net/man/3/shutdown
        if (new_socket != -1)
        {
            if (shutdown(new_socket, SHUT_RDWR) == -1)
            {
                perror("shutdown new_socket");
            }
            if (close(new_socket) == -1)
            {
                perror("close new_socket");
            }
            new_socket = -1;
        }

        if (create_socket != -1)
        {
            if (shutdown(create_socket, SHUT_RDWR) == -1)
            {
                perror("shutdown create_socket");
            }
            if (close(create_socket) == -1)
            {
                perror("close create_socket");
            }
            create_socket = -1;
        }
    }
    else
    {
        exit(sig);
    }
}

void createDir(const std::string& receiverDir)
{
        // Check if the directory already exists, if not, create it
    try 
    {
        fs::create_directories(receiverDir);
        std::string filePath = receiverDir + "/index.txt";
        updateIndex(filePath, 0);
    } 
    catch (const std::exception& e) 
    {
        std::cerr << "Failed to create directory: " << e.what() << std::endl;
    }
}

void writeIntoFile(std::vector<std::string>& parts, const std::string& receiverDir)
{
    std::string filePathIndex = receiverDir + "/index.txt";
    std::ifstream inputFile(filePathIndex);
    int index = 0;
    if (inputFile.is_open()) 
    {
        std::string line;
        // Read and print each line from the file
        std::getline(inputFile, line);
        index = std::stoi(line);
        // Close the file after reading
        inputFile.close();
    }

    index++;

    std::string msgFile = std::to_string(index) + ".txt";
    std::string filePath = receiverDir + "/" + msgFile;
    std::ofstream outputFile(filePath);
    if (outputFile.is_open()) 
    {
        // Write content to the file
        for(int i = 1; i < 5; i++)
            outputFile << parts[i] << '\n';
        outputFile.close();
    }
    updateIndex(filePathIndex, index);
}

void updateIndex(const std::string& filePath, int index)
{
    std::ofstream outputFile(filePath);
    if (outputFile.is_open())
    {
        // Write content to the file
        outputFile << index << '\n';
        outputFile.close();
    }
}

std::string listFiles(const std::string& directoryPath) 
{
    std::string message = "";
    int filenameFound = 0;
    for (const auto& entry : fs::directory_iterator(directoryPath)) 
    {
        if (fs::is_regular_file(entry)) 
        {
            filenameFound++;
        }
    }
    message = std::to_string(filenameFound - 1) + " messages\n";
    
    for (const auto& entry : fs::directory_iterator(directoryPath)) 
    {
        //TODO: amount of files first zero if directory not found or no messages
        if (fs::is_regular_file(entry) && entry.path().filename().string() != "index.txt") 
        {
            message = message + entry.path().filename().string() + '\n';
        }
    }

    return message;
}

void deleteFile(const std::string& pathToFileToDelete)
{
    try 
    {
        // Use std::filesystem::remove to delete the file
        fs::remove(pathToFileToDelete);
    } 
    catch (const std::exception& e) 
    {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}

std::string readFile(const std::string& pathToFileToDelete)
{
    std::string message = "OK\n";
    std::ifstream inputFile(pathToFileToDelete); // Datei im Lese-Modus öffnen
    if (inputFile.is_open()) 
    {
        std::string line;
        
        // Lesen Sie die Datei Zeile für Zeile
        while (std::getline(inputFile, line)) 
        {
            message = message + line + '\n';
        }

        // Schließen Sie die Datei nach dem Lesen
        inputFile.close();
    }
    else
    {
        message = "ERR";
    }

    return message;
}