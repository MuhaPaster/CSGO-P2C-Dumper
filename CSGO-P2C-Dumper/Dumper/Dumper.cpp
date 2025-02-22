#include "Dumper.h"

bool C_Dumper::DumpCheatModule_ByFoundDirectJmp()
{
    LOG("[!] Attempting to dump cheat module by scanning direct JMP's from placed hooks...\n\n");

    if (!this->m_FoundDirectJmpList.size())
    {
        LOG("[-] There were no direct JMP's found...\n")
        return false;
    }

    int m_nCorrectIteration = 0;
    for (int i = 1; i < this->m_FoundDirectJmpList.size(); i++)
    {
        if (this->m_FoundDirectJmpList[m_nCorrectIteration].second > this->m_FoundDirectJmpList[i].second)
        {
            m_nCorrectIteration = i;
        }
    }

    // Calculate lowest offset of direct JMP's, so as to not uselessly throw away valuable codenz.
    uint32_t nAddressToInitiallyDumpFrom = this->m_FoundDirectJmpList[m_nCorrectIteration].second;
    if (!nAddressToInitiallyDumpFrom)
    {
        LOG("[-] Something went wrong while retrieving the direct JMP's gathered...\n")
        return false;
    }

    // 3MB forwards, 2MB backwards..
    // TODO; Dynamically get the size somehow, of this hidden module we're trying to dump.. ?
    const DWORD m_GuessedStartOfModule = nAddressToInitiallyDumpFrom - 250000;
    const DWORD m_GuessedEndOfModule = nAddressToInitiallyDumpFrom + 4000000;

    for (auto& DirectJumpList : this->m_FoundDirectJmpList)
    {
        const uint32_t m_OffsetFromBase = DirectJumpList.second - m_GuessedStartOfModule;
        LOG("[++] Function %s is at ImageBase[+0x%X]\n", DirectJumpList.first, m_OffsetFromBase);
    }

    SIZE_T NumberOfBytesRead = 0;
    BOOL m_bRPMStatusResult = ReadProcessMemory(
        g_Utilities.TargetProcess,
        (LPCVOID)m_GuessedStartOfModule,
        &m_ProcessBuffer,
        8000000,
       &NumberOfBytesRead
    );

    if (!NumberOfBytesRead || (!m_bRPMStatusResult && !m_ProcessBuffer))
    {
        for (int i = 3; i > 0; --i)
        {
            m_bRPMStatusResult = ReadProcessMemory(
                g_Utilities.TargetProcess,
                (LPCVOID)(nAddressToInitiallyDumpFrom - (i * 100000)),
                &m_ProcessBuffer,
                8000000,
                &NumberOfBytesRead
            );

            if (NumberOfBytesRead && m_bRPMStatusResult)
                break;
        }

        if (!NumberOfBytesRead || (!m_bRPMStatusResult && !m_ProcessBuffer))
        {
            LOG("\n[-] Failed to ReadProcessMemory desired address(es) [%i | %i]...\n", NumberOfBytesRead, m_bRPMStatusResult);
            return false;
        }
    }

    LOG("\n[+] Successfully read and copied over the wish bytes from desired process "
        "within the address space: 0x%X -> 0x%X. Original address was 0x%X.\n\n",
        nAddressToInitiallyDumpFrom, m_GuessedEndOfModule, nAddressToInitiallyDumpFrom);

    const HANDLE hFile = CreateFileW((L"Dumps\\DirectJmp-Dump.bin"), 0xC0000000, 0, 0, 2, 0x80, 0);

    try
    {
        if (hFile && hFile != INVALID_HANDLE_VALUE)
        {
            if (!WriteFile(hFile, (LPCVOID)m_ProcessBuffer, 8000000, 0, 0))
            {
                LOG("[-] WriteFile failed, GetLastError() returned %i.\n", GetLastError());
            }
        }
    }

    catch (...)
    {

    }

    if (hFile && hFile != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hFile);
    }

    return true;
}

int C_Dumper::ScanInitialAllocations()
{
    char* CurrentFoundAddress = { 0 };
    _SYSTEM_INFO SystemInfo = { 0 };
    GetSystemInfo(&SystemInfo);

    MEMORY_BASIC_INFORMATION mbi;
    while (CurrentFoundAddress < SystemInfo.lpMaximumApplicationAddress)
    {
        VirtualQueryEx(g_Utilities.TargetProcess, CurrentFoundAddress, &mbi, (SIZE_T)0x1C);

        this->m_PreInjectionAllocatedMemory.push_back({ (char*)mbi.BaseAddress, mbi.RegionSize });
        CurrentFoundAddress = (char*)mbi.BaseAddress + mbi.RegionSize;
        ++m_nPreInjectionPageCount;
    }
    return m_nPreInjectionPageCount;
}

void C_Dumper::DumpCheatModule_ByNewAllocations()
{
    LOG("[!] Attempting to dump cheat module by scanning new allocations...\n\n");

    C_Utilities::Module_t m_DummyModule = { 0 };
    if (!g_Utilities.SetupDesiredModule(("csgo.exe"), &m_DummyModule))
    {
        LOG("[-] Couldn't find main module of desired process...\n");
        return;
    }

    char* CurrentFoundAddress = { 0 };
    _SYSTEM_INFO SystemInfo = { 0 };
    GetSystemInfo(&SystemInfo);

    const HANDLE hFile = CreateFileW((L"Dumps\\AllocatedMemory-Dump.bin"), 0xC0000000, 0, 0, 2, 0x80, 0);

    MEMORY_BASIC_INFORMATION mbi;
    while (CurrentFoundAddress < SystemInfo.lpMaximumApplicationAddress)
    {
        VirtualQueryEx(g_Utilities.TargetProcess, CurrentFoundAddress, &mbi, (SIZE_T)0x1C);

        this->m_PostInjectionAllocatedMemory.push_back({ (char*)mbi.BaseAddress, mbi.RegionSize });
        CurrentFoundAddress = (char*)mbi.BaseAddress + mbi.RegionSize;

        auto shit = std::find_if(m_PreInjectionAllocatedMemory.begin(), m_PreInjectionAllocatedMemory.end(), [&](const AllocatedMemoryInformation_t& o) {
            return o.BaseAddress == mbi.BaseAddress;
            });

        if (shit == m_PreInjectionAllocatedMemory.end())
        {
            LOG("[+] Found new allocation at 0x%X with a size of 0x%X\n", (DWORD)mbi.BaseAddress, mbi.RegionSize);

            SIZE_T NumberOfBytesRead;

            BOOL m_bRPMStatusResult = ReadProcessMemory(
                g_Utilities.TargetProcess,
                (LPCVOID)mbi.BaseAddress,
                &ByteBuf,
                mbi.RegionSize,
                &NumberOfBytesRead
            );

            if (NumberOfBytesRead && m_bRPMStatusResult)
            {
                if (hFile && hFile != INVALID_HANDLE_VALUE)
                {
                    try
                    {
                        WriteFile(hFile, (LPCVOID)ByteBuf, NumberOfBytesRead, 0, 0);
                    }

                    catch (...)
                    {

                    }

                }
                else
                {
                    LOG("[-] CreateFileW failed, GetLastError() returned %i.\n", GetLastError());
                }
            }

        }

        ++m_nPostInjectionPageCount;
    }

    if (hFile && hFile != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hFile);
    }       
}

void C_Dumper::PopulateCheatSignatureTable()
{
    // Onetap signatures
    {
        // CLagCompensation::InvalidateBoneCache 
        m_PayCheatCommonSignatureList.push_back({ "Onetap::InvalidateBoneCache", "\xE8\x00\x00\x00\x00\xC7\x46\x00\x00\x00\x00\x00\x47", "x????xx?????x" });
   
        // CLagCompensation::LocalActivityFix
        m_PayCheatCommonSignatureList.push_back({ "Onetap::LocalActivityFix", "\xE8\x00\x00\x00\x00\x8B\x57\x64", "x????xxx" });

        // CRageBot::UnlockMaxChoke
        m_PayCheatCommonSignatureList.push_back({ "Onetap::UnlockMaxChoke", "\xE8\x00\x00\x00\x00\x8B\x0D\x00\x00\x00\x00\x33\x0D\x00\x00\x00\x00\xE8\x00\x00\x00\x00\xFF\x75\x10", "x????xx????xx????x????xxx" });
   
        // CVisuals::RevealRanks
        m_PayCheatCommonSignatureList.push_back({ "Onetap::RevealRanks", "\x55\x8B\xEC\x83\xEC\x10\x80\x3D\x00\x00\x00\x00\x00", "xxxxxxxx?????" });
   
        // CHooks::InPrediction
        m_PayCheatCommonSignatureList.push_back({ "Onetap::InPrediction", "\x55\x8B\xEC\x56\x8B\x35\x00\x00\x00\x00\xBA\x00\x00\x00\x00", "xxxxxx????x????" });
    }

    // Skeet signatures
    {
        // CAntiAim::CalculatePitch
        m_PayCheatCommonSignatureList.push_back({ "Skeet::CalculatePitch", "\x55\x8B\xEC\x83\xE4\xF8\x51\x51\x8B\x45\x0C\x0F\x28\xD8\xF3\x0F\x11\x5C\x24\x00\x0F\x28\xE1", "xxxxxxxxxxxxxxxxxxx?xxx" });

        // CSDK::SetViewAnglesWrapper
        m_PayCheatCommonSignatureList.push_back({ "Skeet::SetViewAnglesWrapper", "\x55\x8B\xEC\x51\x56\x8B\xF1\x0F\x57\xC0\xF3\x0F\x10\x0E\x0F\x2E\xC9\x9F\xF6\xC4\x44\x7B\x06\x83\x26\x00\x0F\x57\xC9\xF3\x0F\x10\x56\x00\x0F\x2E\xD2", "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx?xxx" });

        // CLagCompensation::DetectPredictionError
        m_PayCheatCommonSignatureList.push_back({ "Skeet::DetectPredictionError", "\x51\x56\x8B\x74\x24\x10\x33\xD2\x57\x8B\xC6\x89\x4C\x24\x08\xBF\x00\x00\x00\x00\xF7\xF7\x6B\xD2\x3C\x03\x51\x44\x39\x32\x0F\x85\x00\x00\x00\x00\x8B\x74\x24\x10", "xxxxxxxxxxxxxxxx????xxxxxxxxxxxx????xxxx" });

        // CRagebot::ApplyAimStep
        m_PayCheatCommonSignatureList.push_back({ "Skeet::ApplyAimStep", "\x55\x8B\xEC\x83\xE4\xF8\x83\xEC\x18\x8B\x45\x14\x0F\x57\xDB\x56\x57\x8B\x7D\x08\xF3\x0F\x10\x10\xF3\x0F\x10\x60\x00\xF3\x0F\x10\x68\x00", "xxxxxxxxxxxxxxxxxxxxxxxxxxxx?xxxx?" });

        // CLagCompensation::FixAnimationLayers
        m_PayCheatCommonSignatureList.push_back({ "Skeet::FixAnimationLayers", "\x55\x8B\xEC\xA1\x00\x00\x00\x00\x81\xEC\x00\x00\x00\x00\x53\x56\x8B\x30\x33\x35\x00\x00\x00\x00\x57\x8B\xF9\x83\x7C\x3E\x00\x00\x0F\x8E\x00\x00\x00\x00", "xxxx????xx????xxxxxx????xxxxxx??xx????" });

        // CLagCompensation::UpdateInterpolation
        m_PayCheatCommonSignatureList.push_back({ "Skeet::UpdateInterpolation", "\x57\x8B\xFA\x85\xF6\x74\x77\x8B\x46\x60\xC1\xE8\x0E\xA8\x01\x74\x6D\xF3\x0F\x10\x4E\x00", "xxxxxxxxxxxxxxxxxxxxx?" });
    }

    // Miscellaneous signatures.
    {
    }
}

bool C_Dumper::DumpCheatModule_ByPopularSignatures()
{
    LOG("[!] Attempting to dump cheat module by scanning %i suspected pay cheat signatures...\n\n", m_PayCheatCommonSignatureList.size());
   
    this->PopulateCheatSignatureTable();

    C_Utilities::Module_t m_DummyModule = { 0 };
    if (!g_Utilities.SetupDesiredModule(("csgo.exe"), &m_DummyModule))
    {
        LOG("[-] Couldn't get desired main process module...\n");
        return false;
    }

    bool m_bHasFoundDesiredSignature = false;
    DWORD m_FoundSignature = NULL;

    for (auto [m_szFunctionName, m_szSignature, m_szMask] : this->m_PayCheatCommonSignatureList)
    {
        m_FoundSignature = g_Utilities.FindSignature(
            m_DummyModule.dwBase,
            m_DummyModule.dwSize,
            m_szSignature,
            m_szMask
        );

        if (m_FoundSignature != 0x0)
        {
            LOG("[+] Found desired signature %s at 0x%X!\n", m_szFunctionName, m_FoundSignature);
            m_bHasFoundDesiredSignature = true;
            break;
        }
    }

    if (!m_bHasFoundDesiredSignature || !m_FoundSignature)
    {
        LOG("[-] Couldn't find any desired signatures %i %i...\n", m_bHasFoundDesiredSignature, m_FoundSignature);
        return false;
    }

    // 3MB forwards, 2MB backwards..
    // TODO; Dynamically get the size somehow, of this hidden module we're trying to dump..
    const DWORD m_GuessedStartOfModule = m_FoundSignature - 200000;
    const DWORD m_GuessedEndOfModule   = m_FoundSignature + 3000000;

    SIZE_T NumberOfBytesRead = 0;
    BOOL m_bRPMStatusResult = ReadProcessMemory(
        g_Utilities.TargetProcess,
        (LPCVOID)m_GuessedStartOfModule,
        &m_ProcessBuffer,
        6000000,
        &NumberOfBytesRead
    );

    if (!m_bRPMStatusResult && !m_ProcessBuffer)
    {
        LOG("[-] Failed to ReadProcessMemory desired address(es)...\n");
        return false;
    }

    LOG("[+] Successfully read and copied over the wish bytes from desired process "
        "within the address space: 0x%X -> 0x%X. Original address was 0x%X.\n",
        m_GuessedStartOfModule, m_GuessedEndOfModule, m_FoundSignature);

    const HANDLE hFile = CreateFileW((L"Dumps\\SignatureSet-Dump.bin"), 0xC0000000, 0, 0, 2, 0x80, 0);

    if (hFile && hFile != INVALID_HANDLE_VALUE)
    {
        try
        {
            WriteFile(hFile, (LPCVOID)ByteBuf, NumberOfBytesRead, 0, 0);
        }

        catch (...)
        {

        }

        CloseHandle(hFile);
    }
 
    return true;
}

