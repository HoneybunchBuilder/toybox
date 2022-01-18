#[===[.md:
# vcpkg_from_sourceforge

Download and extract a project from sourceforge.

## Usage:
```cmake
vcpkg_from_sourceforge(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO <cunit/CUnit>
    [REF <2.1-3>]
    SHA512 <547b417109332...>
    FILENAME <CUnit-2.1-3.tar.bz2>
    [DISABLE_SSL]
    [NO_REMOVE_ONE_LEVEL]
    [PATCHES <patch1.patch> <patch2.patch>...]
)
```

## Parameters:
### OUT_SOURCE_PATH
Specifies the out-variable that will contain the extracted location.

This should be set to `SOURCE_PATH` by convention.

### REPO
The organization or user and repository (optional) on sourceforge.

### REF
A stable version number that will not change contents.

### FILENAME
The local name for the file. Files are shared between ports, so the file may need to be renamed to make it clearly attributed to this port and avoid conflicts.

For example, we can get the download link:
https://sourceforge.net/settings/mirror_choices?projectname=mad&filename=libmad/0.15.1b/libmad-0.15.1b.tar.gz&selected=nchc
So the REPO is `mad/libmad`, the REF is `0.15.1b`, and the FILENAME is `libmad-0.15.1b.tar.gz`

For some special links:
https://sourceforge.net/settings/mirror_choices?projectname=soxr&filename=soxr-0.1.3-Source.tar.xz&selected=nchc
The REPO is `soxr`, REF is not exist, and the FILENAME is `soxr-0.1.3-Source.tar.xz`

### SHA512
The SHA512 hash that should match the archive.

### WORKING_DIRECTORY
If specified, the archive will be extracted into the working directory instead of `${CURRENT_BUILDTREES_DIR}/src/`.

Note that the archive will still be extracted into a subfolder underneath that directory (`${WORKING_DIRECTORY}/${REF}-${HASH}/`).

### PATCHES
A list of patches to be applied to the extracted sources.

Relative paths are based on the port directory.

### DISABLE_SSL
Disable ssl when downloading source.

### NO_REMOVE_ONE_LEVEL
Specifies that the default removal of the top level folder should not occur.

## Examples:

* [cunit](https://github.com/Microsoft/vcpkg/blob/master/ports/cunit/portfile.cmake)
* [polyclipping](https://github.com/Microsoft/vcpkg/blob/master/ports/polyclipping/portfile.cmake)
* [tinyfiledialogs](https://github.com/Microsoft/vcpkg/blob/master/ports/tinyfiledialogs/portfile.cmake)
#]===]

function(vcpkg_from_sourceforge)
    set(booleanValueArgs DISABLE_SSL NO_REMOVE_ONE_LEVEL)
    set(oneValueArgs OUT_SOURCE_PATH REPO REF SHA512 FILENAME WORKING_DIRECTORY)
    set(multipleValuesArgs PATCHES)
    # parse parameters such that semicolons in options arguments to COMMAND don't get erased
    cmake_parse_arguments(PARSE_ARGV 0 _vdus "${booleanValueArgs}" "${oneValueArgs}" "${multipleValuesArgs}")

    if(NOT DEFINED _vdus_OUT_SOURCE_PATH)
        message(FATAL_ERROR "OUT_SOURCE_PATH must be specified.")
    endif()

    if(NOT DEFINED _vdus_SHA512)
        message(FATAL_ERROR "SHA512 must be specified.")
    endif()

    if(NOT DEFINED _vdus_REPO)
        message(FATAL_ERROR "The sourceforge repository must be specified.")
    endif()

    if(DEFINED _vdus_WORKING_DIRECTORY)
        set(WORKING_DIRECTORY WORKING_DIRECTORY "${_vdus_WORKING_DIRECTORY}")
    else()
        set(WORKING_DIRECTORY)
    endif()

    if (_vdus_DISABLE_SSL)
        set(URL_PROTOCOL http:)
    else()
        set(URL_PROTOCOL https:)
    endif()
    
    set(SOURCEFORGE_HOST ${URL_PROTOCOL}//sourceforge.net/projects)

    string(FIND ${_vdus_REPO} "/" FOUND_ORG)
    if (NOT FOUND_ORG EQUAL -1)
        string(SUBSTRING "${_vdus_REPO}" 0 ${FOUND_ORG} ORG_NAME)
        math(EXPR FOUND_ORG "${FOUND_ORG} + 1") # skip the slash
        string(SUBSTRING "${_vdus_REPO}" ${FOUND_ORG} -1 REPO_NAME)
        if (REPO_NAME MATCHES "/")
            message(FATAL_ERROR "REPO should contain at most one slash (found ${_vdus_REPO}).")
        endif()
        set(ORG_NAME ${ORG_NAME}/)
    else()
        set(ORG_NAME ${_vdus_REPO}/)
        set(REPO_NAME )
    endif()
    
    if (DEFINED _vdus_REF)
        set(URL "${SOURCEFORGE_HOST}/${ORG_NAME}files/${REPO_NAME}/${_vdus_REF}/${_vdus_FILENAME}")
    else()
        set(URL "${SOURCEFORGE_HOST}/${ORG_NAME}${REPO_NAME}/files/${_vdus_FILENAME}")
    endif()
        
    set(NO_REMOVE_ONE_LEVEL )
    if (_vdus_NO_REMOVE_ONE_LEVEL)
        set(NO_REMOVE_ONE_LEVEL "NO_REMOVE_ONE_LEVEL")
    endif()

    string(SUBSTRING "${_vdus_SHA512}" 0 10 SANITIZED_REF)

    set(Z_VCPKG_SOURCEFORGE_MIRRORS ${SOURCEFORGE_MIRRORS})
    list(APPEND Z_VCPKG_SOURCEFORGE_MIRRORS
        cfhcable        # United States
        pilotfiber      # New York, NY
        gigenet         # Chicago, IL
        versaweb        # Las Vegas, NV
        ayera           # Modesto, CA
        netactuate      # Durham, NC
        phoenixnap      # Tempe, AZ
        astuteinternet  # Vancouver, BC
        freefr          # Paris, France
        netcologne      # Cologne, Germany
        deac-riga       # Latvia
        excellmedia     # Hyderabad, India
        iweb            # Montreal, QC
        jaist           # Nomi, Japan
        jztkft          # Mezotur, Hungary
        managedway      # Detroit, MI
        nchc            # Taipei, Taiwan
        netix           # Bulgaria
        ufpr            # Curitiba, Brazil
        tenet           # Wynberg, South Africa
    )

    set(URLS "${URL}/download")
    foreach(SOURCEFORGE_MIRROR IN LISTS Z_VCPKG_SOURCEFORGE_MIRRORS)
        list(APPEND URLS "${URL}/download?use_mirror=${SOURCEFORGE_MIRROR}")
    endforeach()

    vcpkg_download_distfile(ARCHIVE
        URLS ${URLS}
        SHA512 "${_vdus_SHA512}"
        FILENAME "${_vdus_FILENAME}"
    )

    vcpkg_extract_source_archive_ex(
        OUT_SOURCE_PATH SOURCE_PATH
        ARCHIVE "${ARCHIVE}"
        REF "${SANITIZED_REF}"
        ${NO_REMOVE_ONE_LEVEL}
        ${WORKING_DIRECTORY}
        PATCHES ${_vdus_PATCHES}
    )

    set(${_vdus_OUT_SOURCE_PATH} "${SOURCE_PATH}" PARENT_SCOPE)
endfunction()
