main(){
	pthread_mutex_init(mutex, attr=1)
	pthread_mutex_init(next, attr=0)

	// setup to allow persistent user input
	while (1)
	{
		if(list.Count()>0)
		{
			handler()
		}
	}
	pthread_mutex_destroy(mutex)
	pthread_mutex_destroy(next)
}



	if(type==rcv)
	{
		newDataNode.data=udpReceive(dataNode.data)
		newDataNode.type=printOut
		prependList(newDataNode)
	}


		if(type==kbIn)
	{
		// note: newStr is already printed on process's screen as a result of inputting, no need to printOut
		newDataNode.data=newStr
		newDataNode.type=send
		prependList(newDataNode)
	}

// evaluating the list item at the tail of the list
// node.data=struct dataNode={enum type{send,printOut}, char[MAXLENGTH] data}

	type=dataNode.type

	if(type==printOut)
	{
		printf("%s\n",dataNode.data)
	}

	if(type==send)
	{
		udpSend(dataNode.data)
	}
}
