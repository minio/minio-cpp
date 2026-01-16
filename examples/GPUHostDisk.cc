#include <cuda.h>
#include <miniocpp/client.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <fstream>
#include <iostream>

int main(int argc, char *argv[]) {
  char *bufptr;
  size_t bufsize = 10 * 1024 * 1024UL;
  if (argc == 2) {
    bufsize = std::atoi(argv[1]);
  }

  cudaMalloc(&bufptr, bufsize);
  cudaMemset(bufptr, 'A', bufsize);
  cudaStreamSynchronize(0);

  char *hostptr;
  hostptr = (char *)malloc(bufsize);
  cudaMemcpy(hostptr, bufptr, bufsize, cudaMemcpyDeviceToHost);

  // Open the file in binary mode for writing
  std::ofstream file("output.txt", std::ios::binary);
  if (file.is_open()) {
    // Write the buffer to the file
    file.write(hostptr, bufsize);

    // Close the file
    file.close();

    std::cout << "Buffer written to file successfully." << std::endl;
  } else {
    std::cerr << "Error opening file." << std::endl;
  }

  free(hostptr);
  cudaFree(bufptr);

  return 0;
}
