default:
	@gcc udping.c -pthread DieWithMessage.c AddressUtility.c -o udping
clean:
	@rm udping