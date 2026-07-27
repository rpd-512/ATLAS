
module LOD8(
    /* verilator lint_off UNUSED */
    input  logic [7:0] in,
    output logic [2:0] out
);

    logic n0, n2, n3, n4, n6, n7, n9, n23;

    assign n0  = ~(in[4] | in[5]);   // NOR(in4, in5)
    assign n2  = ~(in[6] | in[5]);   // NOR(in6, in5)
    assign n3  = ~(in[3] & n0);      // NAND(in3, n0)
    assign n4  = ~(in[6] | in[7]);   // NOR(in6, in7)
    assign n6  = ~(n4    & n0);      // NAND(n4, n0)
    assign n7  = ~(n3    & n2);      // NAND(n3, n2)
    assign n9  = n4 ? n7 : in[7];    // MUX2(in7, n7, n4) -- sel = n4
    assign n23 = ~(n6    & n4);      // NAND(n6, n4)
    wire U;  // = NOT upper_empty, i.e. "upper 6 bits not all zero"
    assign U = n6 | in[3] | in[2];   // 2× OR2

    assign out[2] = n6;                  // free, no gate
    assign out[1] = n23 & U;             // 1× AND2
    assign out[0] = U ? n9 : in[1];      // 1× MUX2

endmodule

/*
// comb2.sv
module LOD8 (
    input  logic a,
    input  logic b,
    input  logic c,
    input  logic d,
    output logic y
);
    logic w1, w2;

    assign w1 = a & b;
    assign w2 = c | d;
    assign y  = w1 ^ w2;

endmodule
*/