# parallel_computing Coursework

# Guide on how to build and run the project
  1. Download coursework_parallel.cpp and client.py files.
  2. Create a project in an IDE.
  3. Download, install, and link the only library that isn't part of C++ standard ones: Boost.asio
    3.1 Download from [here](https://www.boost.org/users/download/), [here](https://sourceforge.net/projects/boost/), or any convenient source;
    3.2 In cmd run the bootstrap.bat file and "b2 install --prefix=PREFIX", where PREFIX is the directory where you want Boost to be installed;
    3.3 Link the lib and include files in your project properties;
  4. Download files and change directory paths in the **_scan_** function in **_main_**.
  5. Choose the thread number, start the server, and wait for the inverted index to build.
  6. Now you can run as many clients as you want.
