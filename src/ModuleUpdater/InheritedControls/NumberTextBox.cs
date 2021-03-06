using System;
using System.Collections;
using System.ComponentModel;
using System.Drawing;
using System.Data;
using System.Windows.Forms;

namespace ModuleUpdater
{
	/// <summary>
	/// This class inherits from TextBox and creates a textbox which can only accept numbers.  The delete key can be used to delete numbers.
	/// </summary>
	public class NumberTextBox : System.Windows.Forms.TextBox
	{
		public NumberTextBox()
		{
		}

		protected override void OnKeyPress(KeyPressEventArgs e) 
		{
			// if the key pressed is a digit or the delete key we mark it as unhandled, and pass it down
			if(char.IsDigit(e.KeyChar) || e.KeyChar == (char)8) 
				e.Handled = false;
			else
				e.Handled = true;

			// we need to call the base class OnKeyPress to make sure that everything is handled correctly
			base.OnKeyPress(e);
		} 
	}
}
