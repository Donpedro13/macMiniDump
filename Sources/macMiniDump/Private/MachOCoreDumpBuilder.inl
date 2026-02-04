template<typename T>
bool MachOCoreDumpBuilder::WriteToOStream (const T& data, IRandomAccessBinaryOStream* pOStream)
{
	return WriteToOStream (&data, sizeof data, pOStream);
}

template<typename... T>
struct ThreadStatesPacked {};

template<typename First, typename... Rest>
struct ThreadStatesPacked<First, Rest...> {
	ThreadStatesPacked (const First& first, const Rest&... rest): first (first), rest (rest...) {}

	First						first;
	ThreadStatesPacked<Rest...> rest;
};

template<typename... ThreadStates>
struct ThreadCommandWithThreadStates {
	thread_command						thread_cmd;
	ThreadStatesPacked<ThreadStates...> rawData;
};

template<typename... ThreadStates>
bool MachOCoreDumpBuilder::AddThreadCommand (ThreadStates... threadStates)
{
	// load_commands are using the "variable length struct" C idiom, so we have to get dirty...
	ThreadCommandWithThreadStates<ThreadStates...> rawData = { {}, { threadStates... } };
	rawData.thread_cmd.cmd								   = LC_THREAD;
	rawData.thread_cmd.cmdsize							   = sizeof rawData - 8; // TODO fix

	const size_t	  alignment = alignof (ThreadCommandWithThreadStates<ThreadStates...>);
	UniquePtr<char[]> pData		= MakeUniqueArrayAligned<char> (rawData.thread_cmd.cmdsize, alignment);
	memcpy (pData.get (), &rawData, sizeof rawData - 8); // TODO fix

	m_thread_cmds.emplace_back (UniquePtr<thread_command> (reinterpret_cast<thread_command*> (pData.release ())));

	return true;
}
