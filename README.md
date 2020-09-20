# Networks
Repository for a Internet and networks class.

Project to make a simple P2P application using TCP sockets. The application is written in C and allow users to share files.
The architecture is made of the following components:
- a Bootstrap (BS) node: It is a concurrent node. It waits for new connection on a well-known (to P and SP) port number.
	A BS node performs the following actions:
	* It allows new peers who want to join the network to get to know other super peers.
	* It allows super peers to join and leave the network, and notifies other super peer of the changes.
- some Super Peers (SP):
	* They interact with the BS node for join and leave requests. 
	* They allow peers to join and leave their subnetwork.
	* They receive a list of files shared by each peer.
	* They handle the file search process on behalf of their peers. This task is carried out by forwarding the requests to other super peers until a match is found.
- some Peers (P) for each Super Peer: They can join a subnetwork, share their files, and download from other peers.
