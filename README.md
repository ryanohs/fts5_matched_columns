# Implementing FTS5 auxiliary functions

[FTS5](https://www.sqlite.org/fts5.html) is an extension for SQLite which provides full text search capability. It supports user defined auxiliary functions which are like normal scalar functions but operate on the current search query and matched row. These a powerful way to add functionality to FTS5.

In this post I am going to demonstrate how to create an auxiliary function to return a comma separated list of the column indexes matching the search query and how to compile and link that function for use in queries executed through the Microsoft.Data.Sqlite nuget package in a C# program.

```sql
select
	FirstName,
	LastName,
	Address,
	matched_columns(People)
from People
where People match $query
```

I am deeply indebted to [this post](https://briancoyner.github.io/articles/2022-02-01-sqlite-fts5-aux-functions) demonstrating how to accomplish this in an Objective-C program which I used as the basis for my implementation.

I'll use this schema and data for my example.

```sql
CREATE VIRTUAL TABLE People USING fts5(FirstName, LastName, Address);

INSERT INTO People (FirstName, LastName, Address) VALUES
('John', 'Doe', '123 Main Street, Brownville'),
('Elmer', 'Fudd', '456 Elm Avenue, Oakville'),
('Michael', 'Johnson', '789 Maple Lane, Rivertown'),
('Sarah', 'Williams', '101 Pine Street, Hillside'),
('David', 'Brown', '222 Oak Street, Springdale'),
('Jennifer', 'Davis', '333 Maple Avenue, Oakville'),
('Daniel', 'Hill', '444 Hill Street, Springfield'),
('Emily', 'Anderson', '555 Elm Street, Rivertown');
```

Microsoft.Data.Sqlite provides a built in function for creating user defined scalar functions directly in C#, but you cannot use it to define an FTS5 auxiliary function. Instead, you must implement the function in C.

You will need a C compiler such as gcc. If you are on a Mac or Linux, one may already be included with your system. On Windows you will need to install one.

[Microsoft.Data.Sqlite](https://www.nuget.org/packages/Microsoft.Data.Sqlite/) is distributed with a copy of SQLite named e_sqlite3 via the SQLitePCLRaw.bundle_e_sqlite3 nuget. The maintainer of that project has another [repository](https://github.com/ericsink/cb) containing the SQLite source and libraries distributed as part of the SQLitePCLRaw nuget. You will need the [sqlite3.h](https://github.com/ericsink/cb/blob/master/sqlite3/sqlite3.h) header file and a [library](https://github.com/ericsink/cb/tree/master/bld/bin/e_sqlite3) targeting your system. For Apple Silicon Macs, the dylib I used is [libe_sqlite3.dylib](https://github.com/ericsink/cb/blob/master/bld/bin/e_sqlite3/mac/arm64/libe_sqlite3.dylib). The entire repository is over 1 GB so I just downloaded the files I needed.

I created a new top level folder in my solution and added these files to it. Then I created a file `matched_columns.c` containing the following code.

The first function comes from the FTS5 documentation and gets a reference to the FTS5 API object. The second function implements the logic for the custom auxiliary function. The last function is what we'll call from C# to tell SQLite about the custom function.

```c
#include<stdbool.h>
#include<stddef.h>
#include<string.h>
#include "sqlite3.h"

/*
** Return a pointer to the fts5_api pointer for database connection db.
** If an error occurs, return NULL and leave an error in the database 
** handle (accessible using sqlite3_errcode()/errmsg()).
*/
fts5_api *fts5_api_from_db(sqlite3 *db){
  fts5_api *pRet = 0;
  sqlite3_stmt *pStmt = 0;

  if(SQLITE_OK==sqlite3_prepare(db, "SELECT fts5(?1)", -1, &pStmt, 0) ){
    sqlite3_bind_pointer(pStmt, 1, (void*)&pRet, "fts5_api_ptr", NULL);
    sqlite3_step(pStmt);
  }
  sqlite3_finalize(pStmt);
  return pRet;
}

/*
** Implementation of an auxiliary function that returns a comma separated list
** of the column indexes matched by any phrase in the search query. The output
** columnIndexes are not ordered. Supports up to ~100 columns.
*/
static void matched_columns_imp(
  const Fts5ExtensionApi *ftsAPI,
  Fts5Context *ftsContext,
  sqlite3_context *resultContext,
  int nVal,
  sqlite3_value **apVal
){
  if (nVal != 0) {
        const char *errorMessage = "Wrong number of arguments.";
        sqlite3_result_error(resultContext, errorMessage, -1);
        return;
    }

    // First, obtain the number of columns in the FTS5 table in order to 
    // allocate a string buffer large enough to hold the comma-separated 
    // list of column indexes + null terminator.
    int numberOfColumns = ftsAPI->xColumnCount(ftsContext);
    char *matchedColumns = sqlite3_malloc(((sizeof(char) * numberOfColumns) * 3));

    // Obtain the number of phrases in the current FTS5 search query.
    int numberOfPhrases = ftsAPI->xPhraseCount(ftsContext);

    // Allocate array to track which columns have already been added to the
    // result to prevent duplicates from columns that contain more than one
    // phrase.
    bool *columnSet = sqlite3_malloc(sizeof(bool)*numberOfColumns);
   
    // Iterate over the phrases
    Fts5PhraseIter phraseIterator;
    for(int phraseIndex = 0; phraseIndex < numberOfPhrases; phraseIndex++)
    {
      // Use the xPhraseFirstColumn and xPhraseNextColumn methods to iterate 
      // the columns containing the phrase.
      int columnIndex = 0;
      for (
          ftsAPI->xPhraseFirstColumn(ftsContext, phraseIndex, &phraseIterator, &columnIndex); 
          columnIndex >= 0; 
          ftsAPI->xPhraseNextColumn(ftsContext, &phraseIterator, &columnIndex)
      ) {
      
        // Skip column if already outputted
        if(columnSet[columnIndex] == false)
        {
          // Mark column as outputted
          columnSet[columnIndex] = true;
        
          // For each matched column, append the column index
          // (including a comma if needed)
          const char *formatString = strlen(matchedColumns) > 0 ? ",%d" : "%d";
          char *formattedColumnIndex = sqlite3_mprintf(formatString, columnIndex);
          strncat(matchedColumns, formattedColumnIndex, sizeof(matchedColumns)-strlen(matchedColumns)-1);
          sqlite3_free(formattedColumnIndex);
        }
      }
    }

    // Finally, return the matched columns string through the resultContext. 
    sqlite3_result_text(resultContext, matchedColumns, -1, SQLITE_TRANSIENT);
    sqlite3_free(matchedColumns);
    sqlite3_free(columnSet);
}

/*
** Register matched_columns function. This is the entry point called from .NET.
** Returns SQLITE_OK (0) if successful.
*/
int fts5_register_matched_columns(sqlite3 *db){
    fts5_api *api;
    api = fts5_api_from_db(db);
    if(api == NULL)
    {
      return -1;
    }
    return api->xCreateFunction(api, "matched_columns", NULL, matched_columns_imp, NULL);
}
```

To compile this on MacOS I used gcc (if you type `gcc` in Terminal, it will install itself if not already present).

```
gcc -Wall -dynamiclib -o libmatched_columns.dylib -fPIC matched_columns.c libe_sqlite3.dylib
```

This compiles the matched_columns.c file and links it against the libe_sqlite3.dylib library. I copied the resulting file `libmatched_columns.dylib` into my C# project and set the Build Action to "None" and Copy to Output Directory to "Always Copy".

Next, create a class to allow C# to invoke this C library.

```cs
using System.Runtime.InteropServices;  
using SQLitePCL;  

public static class NativeMethods  
{  
    [DllImport("matched_columns")]  
    public static extern int fts5_register_matched_columns(sqlite3 db);  
}
```

In this code, the DLLImport is platform agnostic. On Windows it will look for matched_columns.DLL, on Linux it will look for matched_columns.so, and on MacOS it will look for libmatched_columns.dylib. The actual rules are more complex, but generally this is the pattern. The method name must match the C method name.

Now you can call this method with an open SQLite connection to register the custom function.

```cs
var query = "Elm*";

using var connection = new SqliteConnection($"Data Source=demo.db");  
connection.Open();

var err = NativeMethods.fts5_register_matched_columns(connection.Handle);  
if (err != 0)  
{  
    throw new Exception("Failed to register matched_columns function. Error code: " + err);  
}  

var command = connection.CreateCommand();  
command.CommandText = @"  
	select
		FirstName,
		LastName,
		Address,
		matched_columns(People)
	from People
	where People match $query
	order by rank
";  
command.Parameters.AddWithValue("$query", query);  
var reader = command.ExecuteReader();  
while (reader.Read())  
{  
	var firstname = reader.GetString(0);
	var lastname = reader.GetString(1);
	var address = reader.GetString(2);
	var matchedColumns = reader.GetString(3);
	Console.WriteLine($"{firstname}|{lastname}|{address}|{matchedColumns}");
}
```

# Example outputs

`Elm*`:

```
Elmer|Fudd|456 Elm Avenue, Oakville|0,2
Emily|Anderson|555 Elm Street, Rivertown|2
```

First row: FirstName and Address matched
Second row: only Address matched

`Hill`:

```
Daniel|Hill|444 Hill Street, Springfield|1,2
```

LastName and Address matched.

# Resources

[FTS5 documentation](https://www.sqlite.org/fts5.html#custom_auxiliary_functions) - Every time I read this I learn something new.
[Microsoft.Data.Sqlite documentation](https://learn.microsoft.com/en-us/dotnet/standard/data/sqlite/)
[e_sqlite3 source code and libraries](https://github.com/ericsink/cb)
[An Objective-C version](https://briancoyner.github.io/articles/2022-02-01-sqlite-fts5-aux-functions/)

Thanks to ChatGPT for generating test data.
