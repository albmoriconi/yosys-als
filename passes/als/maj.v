
module MAJ(A, B, C, Y);
input A, B, C;
output Y;
assign Y = (A & B) | (A & C) | (B & C);
endmodule

