# sf4e_unpack_verified (cmake/sf4e_verified_unpack.cmake): an unchanged whole
# unpack is kept, and a missing file or another archive's receipt unpacks
# again. Run with cmake -DSOURCE_DIR=<repository> -DWORK=<empty folder> -P.
include("${SOURCE_DIR}/cmake/sf4e_verified_unpack.cmake")
function(expect_file path present message)
    if(EXISTS "${path}")
        set(found TRUE)
    else()
        set(found FALSE)
    endif()
    if(NOT found STREQUAL present)
        message(FATAL_ERROR "Verified unpack: ${message}")
    endif()
endfunction()

file(REMOVE_RECURSE "${WORK}")
file(WRITE "${WORK}/content/sdk/notice.txt" "notice")
file(WRITE "${WORK}/content/sdk/lib/library.txt" "library")
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cf "${WORK}/archive.zip" --format=zip sdk
    WORKING_DIRECTORY "${WORK}/content" RESULT_VARIABLE packed)
if(NOT packed EQUAL 0)
    message(FATAL_ERROR "Verified unpack: the fixture archive was not made")
endif()
file(SHA256 "${WORK}/archive.zip" sha256)
set(out "${WORK}/out")
set(files sdk/notice.txt sdk/lib/library.txt)

sf4e_unpack_verified("${WORK}/archive.zip" "${sha256}" "${out}" ${files})
expect_file("${out}/sdk/lib/library.txt" TRUE "the first unpack is missing a file")

# A file the archive does not hold survives only while the unpack is kept.
file(WRITE "${out}/kept.txt" "")
sf4e_unpack_verified("${WORK}/archive.zip" "${sha256}" "${out}" ${files})
expect_file("${out}/kept.txt" TRUE "a whole unpack of the same archive was unpacked again")

file(REMOVE "${out}/sdk/lib/library.txt")
sf4e_unpack_verified("${WORK}/archive.zip" "${sha256}" "${out}" ${files})
expect_file("${out}/sdk/lib/library.txt" TRUE "a deleted file was not unpacked again")
expect_file("${out}/kept.txt" FALSE "a partial unpack was kept")

file(WRITE "${out}/kept.txt" "")
file(WRITE "${out}/unpacked.sha256" "another archive")
sf4e_unpack_verified("${WORK}/archive.zip" "${sha256}" "${out}" ${files})
expect_file("${out}/kept.txt" FALSE "another archive's unpack was kept")

file(REMOVE_RECURSE "${WORK}")
message("Verified unpack passed")
