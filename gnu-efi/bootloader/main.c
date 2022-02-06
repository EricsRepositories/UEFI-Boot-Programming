#include <efi.h>
#include <efilib.h>

EFI_FILE* LoadFile(EFI_FILE* directory, CHAR16* systemPath, EFI_HANDLE imageHandle, EFI_SYSTEM_TABLE* systemTable)
{
	// elf is stored within the assumed file system
	EFI_LOADED_IMAGE_PROTOCOL* loadedImage;
	systemTable->BootServices->HandleProtocol(imageHandle, &gEfiLoadedImageProtocolGuid, (void**)&loadedImage);

	EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* fileSystem;
	systemTable->BootServices->HandleProtocol(loadedImage->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (void**)&fileSystem);

	if (directory == NULL)
	fileSystem->OpenVolume(fileSystem, &directory);

	EFI_FILE* loadedFile;
	EFI_STATUS statusOf = directory->Open(directory, &loadedFile, systemPath, EFI_FILE_MODE_READ, EFI_FILE_READ_ONLY);

	if (statusOf == EFI_SUCCESS)
	{
		return loadedFile;
	}
	else {
		return NULL;
	}
}

#include <elf.h>
typedef unsigned long long size_t;
int memcmp(const void* aptr, const void* bptr, size_t n)
{
	const unsigned char* a = aptr;
	const unsigned char* b = bptr;
	for (size_t i = 0; i < n; i++)
	{
		if (a[i] < b[i])
		{
 			return -1;
		}
		else if (a[i] > b[i]) {
			return 1;
		}
	}

	return 0;
}

EFI_STATUS efi_main (EFI_HANDLE imageHandle, EFI_SYSTEM_TABLE* systemTable) {

	InitializeLib(imageHandle, systemTable);
	Print(L"Attempting to validate the kernel.\n\r");

	EFI_FILE* kernel = LoadFile(NULL, L"kernel.elf", imageHandle, systemTable);
	if (kernel)
	{
		Print(L"Kernel found.\n\r");
	}
	else {
		Print(L"Kernel not accessible.\n\r");
	}

	Elf64_Ehdr kernelHeader;
	{
		UINTN fileInfoSize;
		EFI_FILE_INFO* fileInfo;
		kernel->GetInfo(kernel, &gEfiFileInfoGuid, &fileInfoSize, NULL);
		systemTable->BootServices->AllocatePool(EfiLoaderData, fileInfoSize, (void**)&fileInfo);
		kernel->GetInfo(kernel, &gEfiFileInfoGuid, &fileInfoSize, (void**)&fileInfo);

		UINTN kernelHeaderSize = sizeof(kernelHeader);
		kernel->Read(kernel, &kernelHeaderSize, &kernelHeader);
	}

	if
	(
	memcmp(&kernelHeader.e_ident[EI_MAG0], ELFMAG, SELFMAG) != 0
	|| kernelHeader.e_ident[EI_CLASS] != ELFCLASS64
	|| kernelHeader.e_ident[EI_DATA] != ELFDATA2LSB
	|| kernelHeader.e_type != ET_EXEC
	|| kernelHeader.e_machine != EM_X86_64
	|| kernelHeader.e_version != EV_CURRENT
	)
	{
		Print(L"Kernel not properly formatted.\r\n");
	}
	else {
		Print(L"Kernel header validated.\r\n");
	}

	Elf64_Phdr* phdrs;
	{
		kernel->SetPosition(kernel, kernelHeader.e_phoff);
		UINTN size = kernelHeader.e_phnum * kernelHeader.e_phentsize;
		systemTable->BootServices->AllocatePool(EfiLoaderData, size, (void**)&phdrs);
		kernel->Read(kernel, &size, phdrs);
	}

	for
	(
		Elf64_Phdr* phdr = phdrs;
		(char*)phdr < (char*)phdrs + kernelHeader.e_phnum * kernelHeader.e_phentsize;
		phdr = (Elf64_Phdr*)(char*)phdr + kernelHeader.e_phentsize
	)
	{
		switch (phdr->p_type)
		{
			case PT_LOAD:
			{
				int pages = (phdr->p_memsz + 0x1000 - 1) / 0x1000;
				Elf64_Addr segment = phdr->p_paddr;
				systemTable->BootServices->AllocatePages(AllocateAddress, EfiLoaderData, pages, &segment);

				kernel->SetPosition(kernel, phdr->p_offset);
				UINTN size = phdr->p_filesz;
				kernel->Read(kernel, &size, (void*)segment);
				break;
			}
		}
	}

	Print(L"Kernel uploaded with success.\r\n");

	int (*KernelStart)() = ((__attribute__((sysv_abi)) int (*)() ) kernelHeader.e_entry);

	Print(L"%d\r\n", KernelStart());

	return EFI_SUCCESS; // Exit the UEFI application
}
