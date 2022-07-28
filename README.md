# MultiThreaded-UDP
Using C Multithreading and Networking for a UDP Server/Client
- Calculates Round Trip Time minimum/average/maximum statistics at the end, alongside the total time and received packet count
_________________________________________________
*** Only run and tested on Linux-based machines due to the use of C multi-threading and networking ***

How to use:
  * Download all files, use command 'make' to compile the necessary files.
  * Run seperate instances for Client and Server
  * There are 5 different CLA available to the user to change default settings, and they're as follows:
    * -c Ping Packet Count (integer, default == 0x7fffffff)
    * -i Ping Interval (double, default == 1.0)
    * -p Port Number (default == 33333)
    * -s Packet Size (in Bytes, default == 12)
    * -n No Print (extended print-out toggle, default == off)
    * -S Server Mode (toggle, default == off == Client Mode)
  * For Client mode, there needs to be an IP address specified at the end of the Command Line Arguments

'make clean' removes any files from the compilation process

Credit to [this book](http://cs.baylor.edu/~donahoo/practical/CSockets2/textcode.html) for **some** of the starter networking code.
