module full_adder (
    input wire A,
    input wire B,
    input wire Cin,
    output wire Sum,
    output wire Cout
);
    assign Sum  = A ^ B ^ Cin;
    assign Cout = (A & B) | (B & Cin) | (A & Cin);
endmodule
