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

  if( SQLITE_OK==sqlite3_prepare(db, "SELECT fts5(?1)", -1, &pStmt, 0) ){
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

    // Allocate array to track which columns have already been added to the result to prevent
    // duplicates from columns that contain more than one phrase.
    bool *columnSet = sqlite3_malloc(sizeof(bool)*numberOfColumns);
   
    // Iterate over the phrases
    Fts5PhraseIter phraseIterator;
    for(int phraseIndex = 0; phraseIndex < numberOfPhrases; phraseIndex++)
    {
      // Use the xPhraseFirstColumn and xPhraseNextColumn methods to iterate the columns
      // containing the phrase.
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
        
          // For each matched column, append the column index (including a comma if needed)
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