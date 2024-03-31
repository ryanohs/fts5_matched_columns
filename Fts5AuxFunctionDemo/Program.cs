using Fts5AuxFunctionDemo;
using Microsoft.Data.Sqlite;

var query = "Elm*";

using var connection = new SqliteConnection($"Data Source=../../../demo.db");
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