default:
	@gcc UDPThreadedPing.c -pthread DieWithMessage.c AddressUtility.c -o udping
clean:
	@rm udping