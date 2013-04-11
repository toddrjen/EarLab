using System;
using System.IO;
using System.Collections;
using System.Xml;
using System.Xml.Schema;
using System.Data;

namespace EarLab.ReaderWriters
{
	/// <summary>
	/// Summary description for ReaderWriterASCIIMetadata.
	/// </summary>
	public class ReaderWriterASCIIMetadata
	{
		public class MetaDataLine
		{
			private string dataType, dataName;
			private object dataValue;

			public MetaDataLine(string dataType, string dataName, object dataValue)
			{
				this.dataType = dataType;
				this.dataName = dataName;
				this.dataValue = dataValue;
			}

			public string Type
			{
				get { return this.dataType; }
			}

			public string Name
			{
				get { return this.dataName; }
			}

			public object Value
			{
				get { return this.dataValue; }
			}
		}

		public static object[] Read(string fileName)
		{
			bool fileType = false;
			ArrayList metadataArray = new ArrayList();

			if (!fileType)
				throw new Exception("ReaderWriterASCIIMetadata.Read(fileName): No file type found in .metadata parameter file.");

			object[] returnArray = new object[metadataArray.Count];
			for (int i=0; i<metadataArray.Count; i++)
				returnArray[i] = metadataArray[i];

			return returnArray;
		}
	}
}